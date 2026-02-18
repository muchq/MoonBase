use std::fs;
use std::path::{Path, PathBuf};
use std::process;
use std::time::Instant;

use microgpt::model::ModelMeta;
use microgpt::{
    ChatDataset, Dataset, InferenceGpt, ModelConfig, TensorAdam, TensorGpt, Tokenizer, TrainConfig,
    TrainState, tensor_train_step,
};

/// Abstraction over text and chat datasets so the training loop can be shared.
pub trait TrainingData {
    fn tokenizer(&self) -> &Tokenizer;
    fn encode_step(&self, step: usize) -> Vec<usize>;
    fn mode_name(&self) -> &str;
    fn num_docs(&self) -> usize;
}

impl TrainingData for Dataset {
    fn tokenizer(&self) -> &Tokenizer {
        &self.tokenizer
    }
    fn encode_step(&self, step: usize) -> Vec<usize> {
        let doc = &self.docs[step % self.docs.len()];
        self.tokenizer.encode_doc(doc)
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
    fn encode_step(&self, step: usize) -> Vec<usize> {
        self.encode_conversation(step % self.len())
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
        optimizer_m: m_bytes,
        optimizer_v: v_bytes,
    }
}

fn save_checkpoint(
    model: &TensorGpt,
    tokenizer: &Tokenizer,
    optimizer: &TensorAdam,
    step: usize,
    output: &Path,
) {
    save_tensor_model(model, tokenizer, output);

    let train_state = TrainState {
        step,
        adam_step: optimizer.step_t,
    };
    let state_json = serde_json::to_string_pretty(&train_state).expect("state serialization");
    if let Err(e) = fs::write(output.join("train_state.json"), state_json) {
        eprintln!("error: failed to save train state: {e}");
        process::exit(1);
    }

    let m_bytes = optimizer.save_m_st();
    if let Err(e) = fs::write(output.join("optimizer_m.safetensors"), &m_bytes) {
        eprintln!("error: failed to save optimizer m: {e}");
        process::exit(1);
    }

    let v_bytes = optimizer.save_v_st();
    if let Err(e) = fs::write(output.join("optimizer_v.safetensors"), &v_bytes) {
        eprintln!("error: failed to save optimizer v: {e}");
        process::exit(1);
    }
}

pub fn run_train(
    data: &dyn TrainingData,
    output: PathBuf,
    steps: usize,
    seed: u64,
    lr: f64,
    model_config: ModelConfig,
    device: &microgpt::Device,
    resume: Option<PathBuf>,
    checkpoint_every: usize,
) {
    if let Err(e) = fs::create_dir_all(&output) {
        eprintln!("error: cannot create output directory: {e}");
        process::exit(1);
    }

    let tokenizer = data.tokenizer();

    let (model, start_step, adam_step, optimizer_state) =
        if let Some(ref resume_dir) = resume {
            let rs = load_resume_state(resume_dir, device);
            println!(
                "resuming from step {} (adam_step={})",
                rs.start_step, rs.adam_step
            );
            (
                rs.model,
                rs.start_step,
                rs.adam_step,
                Some((rs.optimizer_m, rs.optimizer_v)),
            )
        } else {
            print_config(data.mode_name(), &model_config, device);
            println!("num docs:    {}", data.num_docs());
            println!("vocab size:  {}", tokenizer.vocab_size);

            let model = TensorGpt::new(tokenizer.vocab_size, seed, model_config, device);
            let num_params: usize = model
                .varmap
                .all_vars()
                .iter()
                .map(|v| v.as_tensor().elem_count())
                .sum();
            println!("num params:  {}", num_params);
            println!();

            (model, 0, 0, None)
        };

    let total_steps = start_step + steps;
    let config = TrainConfig {
        learning_rate: lr,
        num_steps: total_steps,
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
        let tokens = data.encode_step(step);
        let loss = tensor_train_step(&model, &tokens, &mut optimizer, &config, step)
            .unwrap_or_else(|e| {
                eprintln!("error: training step failed: {e}");
                process::exit(1);
            });
        let steps_done = step - start_step + 1;
        let elapsed = train_start.elapsed().as_secs_f64();
        let avg = elapsed / steps_done as f64;
        let eta = avg * (total_steps - step - 1) as f64;
        println!(
            "step {:4} / {:4} | loss {:.4} | {:.1}s/step | eta {}",
            step + 1,
            total_steps,
            loss,
            avg,
            format_eta(eta),
        );

        if checkpoint_every > 0 && steps_done % checkpoint_every == 0 && step + 1 < total_steps {
            println!("saving checkpoint at step {}...", step + 1);
            save_checkpoint(&model, tokenizer, &optimizer, step + 1, &output);
        }
    }

    let total = train_start.elapsed().as_secs_f64();
    println!("\ntraining complete in {}", format_eta(total));

    save_checkpoint(&model, tokenizer, &optimizer, total_steps, &output);

    // Show samples for text mode (non-chat) models.
    if tokenizer.special_tokens.is_none() {
        println!("\n--- samples ---");
        let weights_bytes = model.save_weights_st();
        let inf = InferenceGpt::load_safetensors(
            tokenizer.vocab_size,
            &weights_bytes,
            model.config,
        )
        .unwrap();
        for i in 0..5 {
            let sample = inf.generate(tokenizer.bos, 0.5, seed + i, None, |id| tokenizer.decode(id));
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

fn print_config(mode: &str, config: &ModelConfig, device: &microgpt::Device) {
    let device_name = match device {
        microgpt::Device::Cpu => "cpu",
        _ => "metal",
    };
    println!("microgpt train ({mode}, device={device_name})");
    println!("  n_embd:      {}", config.n_embd);
    println!("  n_head:      {}", config.n_head);
    println!("  n_layer:     {}", config.n_layer);
    println!("  block_size:  {}", config.block_size);
}

// --- Shared I/O helpers (used by both train and infer modules) ---

pub fn save_tensor_model(model: &TensorGpt, tokenizer: &Tokenizer, output: &Path) {
    let weights_bytes = model.save_weights_st();
    let weights_path = output.join("weights.safetensors");
    if let Err(e) = fs::write(&weights_path, &weights_bytes) {
        eprintln!("error: failed to save weights: {e}");
        process::exit(1);
    }
    println!("weights saved to {}", weights_path.display());

    let meta = ModelMeta {
        vocab_size: tokenizer.vocab_size,
        n_embd: model.config.n_embd,
        n_head: model.config.n_head,
        n_layer: model.config.n_layer,
        block_size: model.config.block_size,
        chars: tokenizer.chars.clone(),
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

pub fn load_weights(model_dir: &Path) -> Vec<u8> {
    let st_path = model_dir.join("weights.safetensors");
    fs::read(&st_path).unwrap_or_else(|e| {
        eprintln!("error: cannot read {}: {e}", st_path.display());
        process::exit(1);
    })
}
