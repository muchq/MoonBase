use std::fs;
use std::path::PathBuf;
use std::process;

use clap::{Parser, Subcommand};
use tracing_subscriber::EnvFilter;

use microgpt::model::ModelMeta;
use microgpt::{Adam, Dataset, Gpt, Tokenizer, TrainConfig, generate, train_step};

#[derive(Parser)]
#[command(
    name = "microgpt",
    about = "microgpt — a minimal GPT trainer and generator",
    version = "0.1.0",
    long_about = "A from-scratch GPT implementation in Rust with its own autograd engine.\n\
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
    /// Train a model on a text file (one document per line).
    Train {
        /// Path to the training data file.
        #[arg(long)]
        input: PathBuf,

        /// Directory to save weights and metadata.
        #[arg(long, default_value = "output")]
        output: PathBuf,

        /// Number of training steps.
        #[arg(long, default_value = "1000")]
        steps: usize,

        /// Random seed.
        #[arg(long, default_value = "42")]
        seed: u64,

        /// Learning rate.
        #[arg(long, default_value = "0.01")]
        lr: f64,
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

    /// Show model info from a saved checkpoint.
    Info {
        /// Directory containing meta.json.
        #[arg(long, default_value = "output")]
        model_dir: PathBuf,
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
        } => run_train(input, output, steps, seed, lr),
        Command::Generate {
            model_dir,
            num_samples,
            temperature,
            seed,
        } => run_generate(model_dir, num_samples, temperature, seed),
        Command::Info { model_dir } => run_info(model_dir),
    }
}

fn run_train(input: PathBuf, output: PathBuf, steps: usize, seed: u64, lr: f64) {
    if !input.exists() {
        eprintln!("error: input file not found: {}", input.display());
        process::exit(1);
    }
    if let Err(e) = fs::create_dir_all(&output) {
        eprintln!("error: cannot create output directory: {e}");
        process::exit(1);
    }

    let dataset = Dataset::load(&input).unwrap_or_else(|e| {
        eprintln!("error: {e}");
        process::exit(1);
    });

    println!("num docs:    {}", dataset.docs.len());
    println!("vocab size:  {}", dataset.tokenizer.vocab_size);

    let model = Gpt::new(dataset.tokenizer.vocab_size, seed);
    let params = model.params();
    println!("num params:  {}", params.len());
    println!();

    let config = TrainConfig {
        learning_rate: lr,
        num_steps: steps,
        ..TrainConfig::default()
    };
    let mut adam = Adam::new(params.len());

    for step in 0..steps {
        let doc = &dataset.docs[step % dataset.docs.len()];
        let tokens = dataset.tokenizer.encode_doc(doc);
        let loss = train_step(&model, &tokens, &params, &mut adam, &config, step);
        println!("step {:4} / {:4} | loss {:.4}", step + 1, steps, loss);
    }

    save_model(&model, &dataset.tokenizer, &output);

    // Show a few samples
    println!("\n--- samples ---");
    let tok = &dataset.tokenizer;
    for i in 0..5 {
        let name = generate(&model, tok.bos, 0.5, seed + i, |id| tok.decode(id));
        println!("  {name}");
    }
}

fn run_generate(model_dir: PathBuf, num_samples: usize, temperature: f64, seed: u64) {
    let (model, tokenizer) = load_model(&model_dir);

    for i in 0..num_samples {
        let sample = generate(&model, tokenizer.bos, temperature, seed + i as u64, |id| {
            tokenizer.decode(id)
        });
        println!("sample {:2}: {sample}", i + 1);
    }
}

fn run_info(model_dir: PathBuf) {
    let meta_path = model_dir.join("meta.json");
    let meta_json = fs::read_to_string(&meta_path).unwrap_or_else(|e| {
        eprintln!("error: cannot read {}: {e}", meta_path.display());
        process::exit(1);
    });
    let meta: ModelMeta = serde_json::from_str(&meta_json).unwrap_or_else(|e| {
        eprintln!("error: invalid meta.json: {e}");
        process::exit(1);
    });

    println!("microgpt model");
    println!("  vocab_size:  {}", meta.vocab_size);
    println!("  n_embd:      {}", meta.n_embd);
    println!("  n_head:      {}", meta.n_head);
    println!("  n_layer:     {}", meta.n_layer);
    println!("  block_size:  {}", meta.block_size);
    println!("  charset:     {:?}", meta.chars);

    let weights_path = model_dir.join("weights.json");
    if weights_path.exists() {
        let weights_json = fs::read_to_string(&weights_path).unwrap_or_else(|e| {
            eprintln!("error: cannot read weights: {e}");
            process::exit(1);
        });
        let model = Gpt::load_weights(meta.vocab_size, &weights_json).unwrap_or_else(|e| {
            eprintln!("error: {e}");
            process::exit(1);
        });
        println!("  num_params:  {}", model.params().len());
    }
}

fn save_model(model: &Gpt, tokenizer: &microgpt::Tokenizer, output: &PathBuf) {
    let weights_json = model.save_weights();
    let weights_path = output.join("weights.json");
    if let Err(e) = fs::write(&weights_path, &weights_json) {
        eprintln!("error: failed to save weights: {e}");
        process::exit(1);
    }
    println!("\nweights saved to {}", weights_path.display());

    let meta = ModelMeta {
        vocab_size: tokenizer.vocab_size,
        n_embd: microgpt::N_EMBD,
        n_head: microgpt::N_HEAD,
        n_layer: microgpt::N_LAYER,
        block_size: microgpt::BLOCK_SIZE,
        chars: tokenizer.chars.clone(),
    };
    let meta_json = serde_json::to_string_pretty(&meta).expect("meta serialization");
    let meta_path = output.join("meta.json");
    if let Err(e) = fs::write(&meta_path, &meta_json) {
        eprintln!("error: failed to save metadata: {e}");
        process::exit(1);
    }
    println!("metadata saved to {}", meta_path.display());
}

fn load_model(model_dir: &PathBuf) -> (Gpt, Tokenizer) {
    let meta_path = model_dir.join("meta.json");
    let meta_json = fs::read_to_string(&meta_path).unwrap_or_else(|e| {
        eprintln!("error: cannot read {}: {e}", meta_path.display());
        process::exit(1);
    });
    let meta: ModelMeta = serde_json::from_str(&meta_json).unwrap_or_else(|e| {
        eprintln!("error: invalid meta.json: {e}");
        process::exit(1);
    });

    let weights_path = model_dir.join("weights.json");
    let weights_json = fs::read_to_string(&weights_path).unwrap_or_else(|e| {
        eprintln!("error: cannot read {}: {e}", weights_path.display());
        process::exit(1);
    });
    let model = Gpt::load_weights(meta.vocab_size, &weights_json).unwrap_or_else(|e| {
        eprintln!("error: {e}");
        process::exit(1);
    });

    let tokenizer = Tokenizer {
        bos: meta.chars.len(),
        vocab_size: meta.vocab_size,
        chars: meta.chars,
    };

    (model, tokenizer)
}
