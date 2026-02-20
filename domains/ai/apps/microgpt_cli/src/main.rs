mod infer;
mod train;

use std::path::{Path, PathBuf};
use std::process;
use std::fs;

use clap::{Parser, Subcommand};
use tracing_subscriber::EnvFilter;

use microgpt::{ChatDataset, Dataset, ModelConfig, TrainState};

#[derive(Parser)]
#[command(
    name = "microgpt",
    about = "microgpt — a minimal GPT trainer and generator",
    version = "0.6.1",
    long_about = "A minimal GPT implementation in Rust using candle for tensor ops.\n\
                  Trains BPE-tokenized language models and generates samples.\n\
                  Ported from karpathy's microgpt.py — the complete algorithm,\n\
                  everything else is just efficiency."
)]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Train a model on a text file (one document per line, or JSONL with --chat).
    Train {
        /// Path to the training data file.
        #[arg(long)]
        input: Option<PathBuf>,

        /// Directory to save weights and metadata.
        #[arg(long, default_value = "output")]
        output: PathBuf,

        /// Number of training steps.
        #[arg(long, default_value = "5000")]
        steps: usize,

        /// Random seed.
        #[arg(long, default_value = "42")]
        seed: u64,

        /// Learning rate (default: 0.01; on resume, uses checkpoint value unless overridden).
        #[arg(long)]
        lr: Option<f64>,

        /// Enable chat mode. Input must be JSONL with conversation arrays.
        #[arg(long)]
        chat: bool,

        /// Embedding dimension.
        #[arg(long, default_value = "64")]
        n_embd: usize,

        /// Number of attention heads (must divide n_embd evenly).
        #[arg(long, default_value = "4")]
        n_head: usize,

        /// Number of transformer layers.
        #[arg(long, default_value = "2")]
        n_layer: usize,

        /// Context window size in tokens (should exceed typical document/conversation length).
        #[arg(long, default_value = "256")]
        block_size: usize,

        /// Device to train on: cpu or metal (requires --features metal).
        #[arg(long, default_value = "cpu")]
        device: String,

        /// Data type for model weights: f32, bf16, or f16.
        /// Defaults to bf16 on Metal, f32 otherwise.
        #[arg(long)]
        dtype: Option<String>,

        /// Resume training from a checkpoint directory.
        #[arg(long)]
        resume: Option<PathBuf>,

        /// Number of sequences per gradient update. Larger values improve GPU
        /// utilization and training speed at the cost of more memory per step
        /// (default: 8; on resume, uses checkpoint value unless overridden).
        #[arg(long)]
        batch_size: Option<usize>,

        /// Save checkpoint every N steps (0 = only at end).
        #[arg(long, default_value = "0")]
        checkpoint_every: usize,

        /// BPE vocabulary size. Larger values capture more subwords.
        #[arg(long, default_value = "4096")]
        vocab_size: usize,

        /// Number of steps to linearly ramp LR from 0 to peak. Prevents
        /// early loss spikes from Adam bias correction (default: 200).
        #[arg(long, default_value = "200")]
        warmup_steps: usize,

        /// Skip documents longer than block_size instead of truncating them.
        /// Ensures the model only trains on complete documents/conversations.
        #[arg(long)]
        skip_long: bool,
    },

    /// Generate samples from a trained model.
    Generate {
        /// Directory containing weights.safetensors and meta.json.
        #[arg(long, default_value = "output")]
        model_dir: PathBuf,

        /// Number of samples to generate.
        #[arg(long, default_value = "20")]
        num_samples: usize,

        /// Sampling temperature.
        #[arg(long, default_value = "0.5")]
        temperature: f64,

        /// Random seed.
        #[arg(long, default_value = "42")]
        seed: u64,
    },

    /// Interactive chat with a trained model.
    Chat {
        /// Directory containing weights.safetensors and meta.json.
        /// Defaults to ~/.config/microgpt/default-chat-model.
        #[arg(long)]
        model_dir: Option<PathBuf>,

        /// Sampling temperature.
        #[arg(long, default_value = "0.5")]
        temperature: f64,

        /// Random seed.
        #[arg(long, default_value = "42")]
        seed: u64,
    },

    /// Show model info from a saved checkpoint.
    Info {
        /// Directory containing meta.json.
        #[arg(long, default_value = "output")]
        model_dir: PathBuf,
    },

    /// Export an inference-only model (no optimizer state), optionally in f16.
    Export {
        /// Source model directory.
        #[arg(long)]
        model_dir: PathBuf,

        /// Output directory for the exported model.
        #[arg(long)]
        output: PathBuf,

        /// Export weights in f16 (half precision) instead of f32.
        #[arg(long)]
        half: bool,
    },
}

fn main() {
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::from_default_env())
        .init();

    let cli = Cli::parse();

    match cli.command {
        Command::Train {
            input,
            output,
            steps,
            seed,
            lr,
            chat,
            n_embd,
            n_head,
            n_layer,
            block_size,
            device,
            resume,
            batch_size,
            checkpoint_every,
            dtype,
            vocab_size,
            warmup_steps,
            skip_long,
        } => run_train_command(
            input, output, steps, seed, lr, chat, n_embd, n_head, n_layer,
            block_size, device, resume, batch_size, checkpoint_every, dtype,
            vocab_size, warmup_steps, skip_long,
        ),
        Command::Generate {
            model_dir,
            num_samples,
            temperature,
            seed,
        } => infer::run_generate(model_dir, num_samples, temperature, seed),
        Command::Chat {
            model_dir,
            temperature,
            seed,
        } => {
            let model_dir = model_dir.unwrap_or_else(default_chat_model_dir);
            infer::run_chat(model_dir, temperature, seed);
        }
        Command::Info { model_dir } => infer::run_info(model_dir),
        Command::Export {
            model_dir,
            output,
            half,
        } => infer::run_export(model_dir, output, half),
    }
}

const BREW_MODEL_DIR: &str = "/opt/homebrew/opt/microgpt/share/microgpt/default-chat-model";
const MODEL_FILES: &[&str] = &["meta.json", "tokenizer.json", "weights.safetensors"];

fn default_chat_model_dir() -> PathBuf {
    let home = std::env::var("HOME").unwrap_or_else(|_| ".".to_string());
    let config_dir = PathBuf::from(home).join(".config/microgpt/default-chat-model");
    ensure_model_dir(&config_dir, Path::new(BREW_MODEL_DIR));
    config_dir
}

fn ensure_model_dir(config_dir: &Path, brew_source: &Path) {
    if !config_dir.join("meta.json").exists() && brew_source.join("meta.json").exists() {
        eprintln!("copying default chat model from Homebrew to {}", config_dir.display());
        let _ = fs::create_dir_all(config_dir);
        for name in MODEL_FILES {
            let src = brew_source.join(name);
            if src.exists() {
                let _ = fs::copy(&src, config_dir.join(name));
            }
        }
    }
}

fn run_train_command(
    input: Option<PathBuf>,
    mut output: PathBuf,
    steps: usize,
    seed: u64,
    lr: Option<f64>,
    mut chat: bool,
    n_embd: usize,
    n_head: usize,
    n_layer: usize,
    block_size: usize,
    device: String,
    resume: Option<PathBuf>,
    batch_size: Option<usize>,
    checkpoint_every: usize,
    dtype: Option<String>,
    vocab_size: usize,
    warmup_steps: usize,
    skip_long: bool,
) {
    let model_config = ModelConfig {
        n_embd,
        n_head,
        n_layer,
        block_size,
    };
    if n_embd % n_head != 0 {
        eprintln!("error: n_embd ({n_embd}) must be divisible by n_head ({n_head})");
        process::exit(1);
    }

    // When resuming, load the checkpoint state once for dataset path + dtype check.
    let resume_state: Option<TrainState> = resume.as_ref().and_then(|dir| {
        let state_path = dir.join("train_state.json");
        let json = fs::read_to_string(&state_path).ok()?;
        serde_json::from_str(&json).ok()
    });

    let input_path = if let Some(ref resume_dir) = resume {
        if output.as_os_str() == "output" {
            output = resume_dir.clone();
        }
        if let Some(p) = input {
            p
        } else if let Some(ref state) = resume_state {
            if let Some(ref path_str) = state.dataset_path {
                if let Some(ref mode) = state.dataset_mode {
                    chat = mode == "chat";
                }
                PathBuf::from(path_str)
            } else {
                eprintln!("error: --input required (checkpoint has no saved dataset path)");
                process::exit(1);
            }
        } else {
            eprintln!("error: --input required (checkpoint has no saved dataset info)");
            process::exit(1);
        }
    } else if let Some(p) = input {
        p
    } else {
        eprintln!("error: --input is required for fresh training");
        process::exit(1);
    };

    if !input_path.exists() {
        eprintln!("error: input file not found: {}", input_path.display());
        process::exit(1);
    }

    let dtype_str = dtype.as_deref().unwrap_or_else(|| {
        if device == "metal" { "bf16" } else { "f32" }
    });

    if let Some(ref state) = resume_state {
        if let Some(ref ckpt_dtype) = state.dtype {
            if ckpt_dtype != dtype_str {
                eprintln!(
                    "warning: checkpoint was saved with dtype={ckpt_dtype}, \
                     but --dtype={dtype_str} was requested; \
                     weights will be loaded in their native dtype"
                );
            }
        }
    }

    let device = parse_device(&device);
    let dtype = match dtype_str {
        "f32" => microgpt::DType::F32,
        "bf16" => microgpt::DType::BF16,
        "f16" => microgpt::DType::F16,
        other => {
            eprintln!("error: unknown dtype {other:?} (expected f32, bf16, or f16)");
            process::exit(1);
        }
    };

    if chat {
        let mut data = ChatDataset::load(&input_path, vocab_size).unwrap_or_else(|e| {
            eprintln!("error: {e}");
            process::exit(1);
        });
        if data.trimmed_count > 0 || data.skipped_count > 0 {
            println!(
                "data cleanup: {} conversations trimmed (trailing user turn removed), {} skipped (no assistant turn)",
                data.trimmed_count, data.skipped_count
            );
        }
        if skip_long {
            let removed = data.filter_to_block_size(block_size);
            if removed > 0 {
                println!("--skip-long: dropped {removed} conversations exceeding block_size ({block_size})");
            }
            if data.is_empty() {
                eprintln!("error: all conversations exceed block_size ({block_size}); try a larger --block-size");
                process::exit(1);
            }
        }
        train::run_train(train::TrainArgs {
            data: &data,
            input_path: &input_path,
            output, steps, seed, lr, model_config,
            device: &device, dtype, resume, batch_size, checkpoint_every,
            warmup_steps,
        });
    } else {
        let mut data = Dataset::load(&input_path, vocab_size).unwrap_or_else(|e| {
            eprintln!("error: {e}");
            process::exit(1);
        });
        if skip_long {
            let removed = data.filter_to_block_size(block_size);
            if removed > 0 {
                println!("--skip-long: dropped {removed} documents exceeding block_size ({block_size})");
            }
            if data.docs.is_empty() {
                eprintln!("error: all documents exceed block_size ({block_size}); try a larger --block-size");
                process::exit(1);
            }
        }
        train::run_train(train::TrainArgs {
            data: &data,
            input_path: &input_path,
            output, steps, seed, lr, model_config,
            device: &device, dtype, resume, batch_size, checkpoint_every,
            warmup_steps,
        });
    }
}

fn parse_device(s: &str) -> microgpt::Device {
    match s {
        "cpu" => microgpt::Device::Cpu,
        "metal" => {
            #[cfg(feature = "metal")]
            {
                microgpt::Device::new_metal(0).unwrap_or_else(|e| {
                    eprintln!("error: failed to initialize Metal device: {e}");
                    process::exit(1);
                })
            }
            #[cfg(not(feature = "metal"))]
            {
                eprintln!("error: metal support requires building with --features metal");
                process::exit(1);
            }
        }
        other => {
            eprintln!("error: unknown device {other:?} (expected \"cpu\" or \"metal\")");
            process::exit(1);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    fn temp_dir(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("microgpt_cli_test_{name}_{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn ensure_model_dir_copies_from_brew_source() {
        let root = temp_dir("copy");
        let brew = root.join("brew");
        let config = root.join("config");
        fs::create_dir_all(&brew).unwrap();
        fs::write(brew.join("meta.json"), "{}").unwrap();
        fs::write(brew.join("tokenizer.json"), "{}").unwrap();
        fs::write(brew.join("weights.safetensors"), "data").unwrap();

        ensure_model_dir(&config, &brew);

        assert!(config.join("meta.json").exists());
        assert!(config.join("tokenizer.json").exists());
        assert!(config.join("weights.safetensors").exists());
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn ensure_model_dir_no_op_when_already_exists() {
        let root = temp_dir("noop");
        let brew = root.join("brew");
        let config = root.join("config");
        fs::create_dir_all(&brew).unwrap();
        fs::create_dir_all(&config).unwrap();
        fs::write(brew.join("meta.json"), "new").unwrap();
        fs::write(config.join("meta.json"), "existing").unwrap();

        ensure_model_dir(&config, &brew);

        let content = fs::read_to_string(config.join("meta.json")).unwrap();
        assert_eq!(content, "existing", "should not overwrite existing model");
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn ensure_model_dir_no_op_when_no_brew_source() {
        let root = temp_dir("nobrew");
        let brew = root.join("brew_missing");
        let config = root.join("config");

        ensure_model_dir(&config, &brew);

        assert!(!config.exists(), "should not create config dir without brew source");
        let _ = fs::remove_dir_all(&root);
    }
}
