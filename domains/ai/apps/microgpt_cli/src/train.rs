use std::fs;
use std::path::{Path, PathBuf};
use std::process;
use std::time::Instant;

use microgpt::model::ModelMeta;
use microgpt::{
    ChatDataset, Dataset, InferenceGpt, ModelConfig, TensorAdam, TensorGpt, Tokenizer, TrainConfig,
    TrainState, tensor_train_step_batched,
};

/// Abstraction over text and chat datasets so the training loop can be shared.
pub trait TrainingData {
    fn tokenizer(&self) -> &Tokenizer;
    fn encode_step(&self, index: usize) -> Vec<usize>;
    fn doc_token_len(&self, index: usize) -> usize;
    fn mode_name(&self) -> &str;
    fn num_docs(&self) -> usize;
}

impl TrainingData for Dataset {
    fn tokenizer(&self) -> &Tokenizer {
        &self.tokenizer
    }
    fn encode_step(&self, index: usize) -> Vec<usize> {
        self.tokenized_docs[index % self.tokenized_docs.len()].clone()
    }
    fn doc_token_len(&self, index: usize) -> usize {
        self.tokenized_docs[index % self.tokenized_docs.len()].len()
    }
    fn mode_name(&self) -> &str {
        "text"
    }
    fn num_docs(&self) -> usize {
        self.docs.len()
    }
}

impl TrainingData for ChatDataset {
    fn tokenizer(&self) -> &Tokenizer {
        &self.tokenizer
    }
    fn encode_step(&self, index: usize) -> Vec<usize> {
        self.tokenized_conversations[index % self.tokenized_conversations.len()].clone()
    }
    fn doc_token_len(&self, index: usize) -> usize {
        self.tokenized_conversations[index % self.tokenized_conversations.len()].len()
    }
    fn mode_name(&self) -> &str {
        "chat"
    }
    fn num_docs(&self) -> usize {
        self.len()
    }
}

struct ResumeState {
    model: TensorGpt,
    start_step: usize,
    adam_step: usize,
    learning_rate: f64,
    batch_size: usize,
    dataset_path: Option<String>,
    dataset_mode: Option<String>,
    optimizer_m: Vec<u8>,
    optimizer_v: Vec<u8>,
}

fn load_resume_state(resume_dir: &Path, device: &microgpt::Device) -> ResumeState {
    let meta = load_meta(resume_dir);
    let config = meta.config();

    let weights_bytes = load_weights(resume_dir);
    let model = TensorGpt::load_weights_st(meta.vocab_size, &weights_bytes, config, device)
        .unwrap_or_else(|e| {
            eprintln!("error: failed to load checkpoint weights: {e}");
            process::exit(1);
        });

    let state_path = resume_dir.join("train_state.json");
    let state_json = fs::read_to_string(&state_path).unwrap_or_else(|e| {
        eprintln!("error: cannot read {}: {e}", state_path.display());
        process::exit(1);
    });
    let state: TrainState = serde_json::from_str(&state_json).unwrap_or_else(|e| {
        eprintln!("error: invalid train_state.json: {e}");
        process::exit(1);
    });

    let m_bytes = fs::read(resume_dir.join("optimizer_m.safetensors")).unwrap_or_else(|e| {
        eprintln!("error: cannot read optimizer_m.safetensors: {e}");
        process::exit(1);
    });
    let v_bytes = fs::read(resume_dir.join("optimizer_v.safetensors")).unwrap_or_else(|e| {
        eprintln!("error: cannot read optimizer_v.safetensors: {e}");
        process::exit(1);
    });

    ResumeState {
        model,
        start_step: state.step,
        adam_step: state.adam_step,
        learning_rate: state.learning_rate,
        batch_size: state.batch_size,
        dataset_path: state.dataset_path,
        dataset_mode: state.dataset_mode,
        optimizer_m: m_bytes,
        optimizer_v: v_bytes,
    }
}

fn dtype_to_str(dtype: microgpt::DType) -> &'static str {
    match dtype {
        microgpt::DType::F32 => "f32",
        microgpt::DType::BF16 => "bf16",
        microgpt::DType::F16 => "f16",
        _ => "unknown",
    }
}

/// Write all checkpoint files to a staging directory, then move each file into
/// place. `train_state.json` is moved last so its presence signals a complete
/// checkpoint. If the process is killed mid-save, the worst case is a missing
/// `train_state.json`, which the resume logic already treats as an error.
fn save_checkpoint(
    model: &TensorGpt,
    tokenizer: &Tokenizer,
    optimizer: &TensorAdam,
    step: usize,
    lr: f64,
    batch_size: usize,
    input_path: &Path,
    mode: &str,
    output: &Path,
) {
    let staging = output.join(".ckpt_staging");
    if let Err(e) = fs::create_dir_all(&staging) {
        eprintln!("error: cannot create staging directory: {e}");
        process::exit(1);
    }

    // save_tensor_model writes weights, tokenizer.json, and meta.json
    save_tensor_model(model, tokenizer, &staging);

    let m_bytes = optimizer.save_m_st().unwrap_or_else(|e| {
        eprintln!("error: failed to serialize optimizer m: {e}");
        process::exit(1);
    });
    if let Err(e) = fs::write(staging.join("optimizer_m.safetensors"), &m_bytes) {
        eprintln!("error: failed to save optimizer m: {e}");
        process::exit(1);
    }

    let v_bytes = optimizer.save_v_st().unwrap_or_else(|e| {
        eprintln!("error: failed to serialize optimizer v: {e}");
        process::exit(1);
    });
    if let Err(e) = fs::write(staging.join("optimizer_v.safetensors"), &v_bytes) {
        eprintln!("error: failed to save optimizer v: {e}");
        process::exit(1);
    }

    let train_state = TrainState {
        step,
        adam_step: optimizer.step_t,
        learning_rate: lr,
        batch_size,
        dataset_path: Some(input_path.to_string_lossy().to_string()),
        dataset_mode: Some(mode.to_string()),
        dtype: Some(dtype_to_str(model.dtype).to_string()),
    };
    let state_json = serde_json::to_string_pretty(&train_state).expect("state serialization");
    if let Err(e) = fs::write(staging.join("train_state.json"), &state_json) {
        eprintln!("error: failed to save train state: {e}");
        process::exit(1);
    }

    // Move staged files into the final output directory. On the same
    // filesystem fs::rename is atomic per-file.
    let files = [
        "weights.safetensors",
        "tokenizer.json",
        "meta.json",
        "optimizer_m.safetensors",
        "optimizer_v.safetensors",
        "train_state.json", // moved last — its presence = complete checkpoint
    ];
    for name in &files {
        let src = staging.join(name);
        if src.exists() {
            if let Err(e) = fs::rename(&src, output.join(name)) {
                eprintln!("error: failed to move {name} into output: {e}");
                process::exit(1);
            }
        }
    }

    let _ = fs::remove_dir(&staging);
}

pub struct TrainArgs<'a> {
    pub data: &'a dyn TrainingData,
    pub input_path: &'a Path,
    pub output: PathBuf,
    pub steps: usize,
    pub seed: u64,
    /// `None` means "use checkpoint value on resume, or 0.01 for fresh training".
    pub lr: Option<f64>,
    pub model_config: ModelConfig,
    pub device: &'a microgpt::Device,
    pub dtype: microgpt::DType,
    pub resume: Option<PathBuf>,
    /// `None` means "use checkpoint value on resume, or 8 for fresh training".
    pub batch_size: Option<usize>,
    pub checkpoint_every: usize,
    pub warmup_steps: usize,
}

pub fn run_train(args: TrainArgs) {
    let TrainArgs {
        data,
        input_path,
        output,
        steps,
        seed,
        lr: lr_override,
        model_config,
        device,
        dtype,
        resume,
        batch_size: batch_size_override,
        checkpoint_every,
        warmup_steps,
    } = args;

    if let Err(e) = fs::create_dir_all(&output) {
        eprintln!("error: cannot create output directory: {e}");
        process::exit(1);
    }

    let tokenizer = data.tokenizer();

    let (model, start_step, adam_step, optimizer_state, lr, batch_size) =
        if let Some(ref resume_dir) = resume {
            let rs = load_resume_state(resume_dir, device);
            println!(
                "resuming from step {} (adam_step={})",
                rs.start_step, rs.adam_step
            );
            let resolved = resolve_overrides(
                lr_override,
                batch_size_override,
                rs.learning_rate,
                rs.batch_size,
            );
            if resolved.lr != rs.learning_rate && lr_override.is_some() {
                eprintln!("warning: overriding learning rate: {} -> {}", rs.learning_rate, resolved.lr);
            } else {
                println!("resumed learning rate: {}", resolved.lr);
            }
            if resolved.batch_size != rs.batch_size && batch_size_override.is_some() {
                eprintln!("warning: overriding batch size: {} -> {}", rs.batch_size, resolved.batch_size);
            } else {
                println!("resumed batch size: {}", resolved.batch_size);
            }
            if let Some(path) = rs.dataset_path {
                println!("resumed dataset: {}", path);
            }
            if let Some(mode) = rs.dataset_mode {
                println!("resumed mode: {}", mode);
            }
            (
                rs.model,
                rs.start_step,
                rs.adam_step,
                Some((rs.optimizer_m, rs.optimizer_v)),
                resolved.lr,
                resolved.batch_size,
            )
        } else {
            let resolved = resolve_overrides(lr_override, batch_size_override, 0.01, 8);
            print_config(data.mode_name(), &model_config, device, dtype, resolved.batch_size);
            println!("num docs:    {}", data.num_docs());
            println!("vocab size:  {}", tokenizer.vocab_size);

            let model = TensorGpt::new(tokenizer.vocab_size, seed, model_config, device, dtype);
            let num_params: usize = model
                .varmap
                .all_vars()
                .iter()
                .map(|v| v.as_tensor().elem_count())
                .sum();
            println!("num params:  {}", num_params);

            let mut truncated_count = 0;
            for i in 0..data.num_docs() {
                if data.doc_token_len(i) > model_config.block_size + 1 {
                    truncated_count += 1;
                }
            }
            if truncated_count > 0 {
                println!(
                    "warning: {} / {} docs are longer than block_size ({}) and will be truncated",
                    truncated_count,
                    data.num_docs(),
                    model_config.block_size
                );
            }
            println!();

            (model, 0, 0, None, resolved.lr, resolved.batch_size)
        };

    let total_steps = start_step + steps;
    let config = TrainConfig {
        learning_rate: lr,
        num_steps: total_steps,
        warmup_steps,
        ..TrainConfig::default()
    };
    let mut optimizer = TensorAdam::new(&model.varmap, &config).unwrap_or_else(|e| {
        eprintln!("error: failed to create optimizer: {e}");
        process::exit(1);
    });

    if let Some((m_bytes, v_bytes)) = optimizer_state {
        optimizer
            .load_state_st(&m_bytes, &v_bytes, adam_step)
            .unwrap_or_else(|e| {
                eprintln!("error: failed to restore optimizer state: {e}");
                process::exit(1);
            });
    }

    let train_start = Instant::now();

    for step in start_step..total_steps {
        let step_start = Instant::now();
        
        // Pseudo-shuffle: use a large prime stride to jump around the dataset
        // This avoids processing sorted data sequentially.
        let stride = 997; 
        let num_docs = data.num_docs();
        
        let batch: Vec<Vec<usize>> = (0..batch_size)
            .map(|b| {
                let index = (step * batch_size + b).wrapping_mul(stride) % num_docs;
                data.encode_step(index)
            })
            .collect();
            
        let max_batch_len = batch.iter().map(|s| s.len()).max().unwrap_or(0);

        let loss = tensor_train_step_batched(&model, &batch, &mut optimizer, &config, step)
            .unwrap_or_else(|e| {
                eprintln!("error: training step failed: {e}");
                process::exit(1);
            });

        let steps_done = step - start_step + 1;
        let step_elapsed = step_start.elapsed().as_secs_f64();
        let total_elapsed = train_start.elapsed().as_secs_f64();
        let avg = total_elapsed / steps_done as f64;
        let eta = avg * (total_steps - step - 1) as f64;
        println!(
            "step {:4} / {:4} | len {:4} | loss {:.4} | {:.1}s/step | eta {}",
            step + 1,
            total_steps,
            max_batch_len,
            loss,
            step_elapsed,
            format_eta(eta),
        );

        if checkpoint_every > 0 && steps_done % checkpoint_every == 0 && step + 1 < total_steps {
            println!("saving checkpoint at step {}...", step + 1);
            save_checkpoint(&model, tokenizer, &optimizer, step + 1, config.learning_rate, batch_size, input_path, data.mode_name(), &output);
        }
    }

    let total = train_start.elapsed().as_secs_f64();
    println!("\ntraining complete in {}", format_eta(total));

    save_checkpoint(&model, tokenizer, &optimizer, total_steps, config.learning_rate, batch_size, input_path, data.mode_name(), &output);

    // Show samples for text mode (non-chat) models.
    if tokenizer.special_tokens.is_none() {
        println!("\n--- samples ---");
        let weights_bytes = model.save_weights_st().unwrap_or_else(|e| {
            eprintln!("error: failed to serialize weights for samples: {e}");
            process::exit(1);
        });
        let inf = InferenceGpt::load_safetensors(
            tokenizer.vocab_size,
            &weights_bytes,
            model.config,
        )
        .unwrap();
        for i in 0..5 {
            let tok = &tokenizer;
            let sample = inf.generate(tok.bos, 0.5, seed + i, None, |id| tok.decode(id));
            println!("  {sample}");
        }
    }
}

fn format_eta(secs: f64) -> String {
    let s = secs as u64;
    if s < 60 {
        format!("{s}s")
    } else if s < 3600 {
        format!("{}m {:02}s", s / 60, s % 60)
    } else {
        format!("{}h {:02}m", s / 3600, (s % 3600) / 60)
    }
}

fn print_config(mode: &str, config: &ModelConfig, device: &microgpt::Device, dtype: microgpt::DType, batch_size: usize) {
    let device_name = match device {
        microgpt::Device::Cpu => "cpu",
        _ => "metal",
    };
    println!("microgpt train ({mode}, device={device_name}, dtype={:?})", dtype);
    println!("  n_embd:      {}", config.n_embd);
    println!("  n_head:      {}", config.n_head);
    println!("  n_layer:     {}", config.n_layer);
    println!("  block_size:  {}", config.block_size);
    println!("  batch_size:  {batch_size}");
}

// --- Shared I/O helpers (used by both train and infer modules) ---

pub fn save_tensor_model(model: &TensorGpt, tokenizer: &Tokenizer, output: &Path) {
    let weights_bytes = model.save_weights_st().unwrap_or_else(|e| {
        eprintln!("error: failed to serialize weights: {e}");
        process::exit(1);
    });
    let weights_path = output.join("weights.safetensors");
    if let Err(e) = fs::write(&weights_path, &weights_bytes) {
        eprintln!("error: failed to save weights: {e}");
        process::exit(1);
    }
    println!("weights saved to {}", weights_path.display());

    let tok_path = output.join("tokenizer.json");
    tokenizer.save(&tok_path).unwrap_or_else(|e| {
        eprintln!("error: failed to save tokenizer: {e}");
        process::exit(1);
    });
    println!("tokenizer saved to {}", tok_path.display());

    let meta = ModelMeta {
        vocab_size: tokenizer.vocab_size,
        n_embd: model.config.n_embd,
        n_head: model.config.n_head,
        n_layer: model.config.n_layer,
        block_size: model.config.block_size,
        special_tokens: tokenizer.special_token_names(),
    };
    let meta_json = serde_json::to_string_pretty(&meta).expect("meta serialization");
    let meta_path = output.join("meta.json");
    if let Err(e) = fs::write(&meta_path, &meta_json) {
        eprintln!("error: failed to save metadata: {e}");
        process::exit(1);
    }
    println!("metadata saved to {}", meta_path.display());
}

pub fn load_meta(model_dir: &Path) -> ModelMeta {
    let meta_path = model_dir.join("meta.json");
    let meta_json = fs::read_to_string(&meta_path).unwrap_or_else(|e| {
        eprintln!("error: cannot read {}: {e}", meta_path.display());
        process::exit(1);
    });
    serde_json::from_str(&meta_json).unwrap_or_else(|e| {
        eprintln!("error: invalid meta.json: {e}");
        process::exit(1);
    })
}

struct ResolvedParams {
    lr: f64,
    batch_size: usize,
}

fn resolve_overrides(
    lr_override: Option<f64>,
    batch_size_override: Option<usize>,
    checkpoint_lr: f64,
    checkpoint_batch_size: usize,
) -> ResolvedParams {
    ResolvedParams {
        lr: lr_override.unwrap_or(checkpoint_lr),
        batch_size: batch_size_override.unwrap_or(checkpoint_batch_size),
    }
}

pub fn load_weights(model_dir: &Path) -> Vec<u8> {
    let st_path = model_dir.join("weights.safetensors");
    fs::read(&st_path).unwrap_or_else(|e| {
        eprintln!("error: cannot read {}: {e}", st_path.display());
        process::exit(1);
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn resolve_overrides_defaults_to_checkpoint_values() {
        let r = resolve_overrides(None, None, 0.005, 16);
        assert_eq!(r.lr, 0.005);
        assert_eq!(r.batch_size, 16);
    }

    #[test]
    fn resolve_overrides_lr_override() {
        let r = resolve_overrides(Some(0.001), None, 0.005, 16);
        assert_eq!(r.lr, 0.001);
        assert_eq!(r.batch_size, 16);
    }

    #[test]
    fn resolve_overrides_batch_size_override() {
        let r = resolve_overrides(None, Some(32), 0.005, 16);
        assert_eq!(r.lr, 0.005);
        assert_eq!(r.batch_size, 32);
    }

    #[test]
    fn resolve_overrides_both_overridden() {
        let r = resolve_overrides(Some(0.1), Some(64), 0.005, 16);
        assert_eq!(r.lr, 0.1);
        assert_eq!(r.batch_size, 64);
    }

    #[test]
    fn resolve_overrides_same_value_as_checkpoint() {
        let r = resolve_overrides(Some(0.005), Some(16), 0.005, 16);
        assert_eq!(r.lr, 0.005);
        assert_eq!(r.batch_size, 16);
    }

    #[test]
    fn resolve_overrides_fresh_training_defaults() {
        let r = resolve_overrides(None, None, 0.01, 8);
        assert_eq!(r.lr, 0.01);
        assert_eq!(r.batch_size, 8);
    }

    #[test]
    fn resolve_overrides_fresh_training_with_custom_lr() {
        let r = resolve_overrides(Some(0.05), None, 0.01, 8);
        assert_eq!(r.lr, 0.05);
        assert_eq!(r.batch_size, 8);
    }
}
