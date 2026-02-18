mod infer;
mod train;

use std::path::PathBuf;
use std::process;

use clap::{Parser, Subcommand};
use tracing_subscriber::EnvFilter;

use microgpt::{ChatDataset, Dataset, ModelConfig};

#[derive(Parser)]
#[command(
    name = "microgpt",
    about = "microgpt — a minimal GPT trainer and generator",
    version = "0.3.2",
    long_about = "A minimal GPT implementation in Rust using candle for tensor ops.\n\
                  Trains character-level language models and generates samples.\n\
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
        input: PathBuf,

        /// Directory to save weights and metadata.
        #[arg(long, default_value = "output")]
        output: PathBuf,

        /// Number of training steps.
        #[arg(long, default_value = "5000")]
        steps: usize,

        /// Random seed.
        #[arg(long, default_value = "42")]
        seed: u64,

        /// Learning rate.
        #[arg(long, default_value = "0.01")]
        lr: f64,

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

        /// Resume training from a checkpoint directory.
        #[arg(long)]
        resume: Option<PathBuf>,

        /// Save checkpoint every N steps (0 = only at end).
        #[arg(long, default_value = "0")]
        checkpoint_every: usize,
    },

    /// Generate samples from a trained model.
    Generate {
        /// Directory containing weights.json and meta.json.
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
        /// Directory containing weights.json and meta.json.
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
            checkpoint_every,
        } => {
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
            if !input.exists() {
                eprintln!("error: input file not found: {}", input.display());
                process::exit(1);
            }
            let device = parse_device(&device);
            if chat {
                let data = ChatDataset::load(&input).unwrap_or_else(|e| {
                    eprintln!("error: {e}");
                    process::exit(1);
                });
                train::run_train(
                    &data,
                    output,
                    steps,
                    seed,
                    lr,
                    model_config,
                    &device,
                    resume,
                    checkpoint_every,
                );
            } else {
                let data = Dataset::load(&input).unwrap_or_else(|e| {
                    eprintln!("error: {e}");
                    process::exit(1);
                });
                train::run_train(
                    &data,
                    output,
                    steps,
                    seed,
                    lr,
                    model_config,
                    &device,
                    resume,
                    checkpoint_every,
                );
            }
        }
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
            let model_dir = model_dir.unwrap_or_else(|| {
                let home = std::env::var("HOME").unwrap_or_else(|_| ".".to_string());
                PathBuf::from(home).join(".config/microgpt/default-chat-model")
            });
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
