use std::fs;
use std::io::{self, Write};
use std::path::PathBuf;
use std::process;
use std::time::Instant;

use clap::{Parser, Subcommand};
use tracing_subscriber::EnvFilter;

use candle_core::Tensor;
use microgpt::model::ModelMeta;
use microgpt::{
    ChatDataset, Dataset, InferenceGpt, ModelConfig, Tokenizer, TrainConfig,
    TensorGpt, TensorAdam, tensor_train_step,
};

#[derive(Parser)]
#[command(
    name = "microgpt",
    about = "microgpt — a minimal GPT trainer and generator",
    version = "0.1.1",
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
    /// Train a model on a text file (one document per line, or JSONL with --chat).
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

        /// Enable chat mode. Input must be JSONL with conversation arrays.
        #[arg(long)]
        chat: bool,

        /// Embedding dimension.
        #[arg(long, default_value = "16")]
        n_embd: usize,

        /// Number of attention heads (must divide n_embd evenly).
        #[arg(long, default_value = "4")]
        n_head: usize,

        /// Number of transformer layers.
        #[arg(long, default_value = "1")]
        n_layer: usize,

        /// Context window size in tokens.
        #[arg(long, default_value = "16")]
        block_size: usize,
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
        #[arg(long, default_value = "output")]
        model_dir: PathBuf,

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
            chat,
            n_embd,
            n_head,
            n_layer,
            block_size,
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
            if chat {
                run_train_chat(input, output, steps, seed, lr, model_config);
            } else {
                run_train(input, output, steps, seed, lr, model_config);
            }
        }
        Command::Generate {
            model_dir,
            num_samples,
            temperature,
            seed,
        } => run_generate(model_dir, num_samples, temperature, seed),
        Command::Chat {
            model_dir,
            temperature,
            seed,
        } => run_chat(model_dir, temperature, seed),
        Command::Info { model_dir } => run_info(model_dir),
    }
}

fn run_train(
    input: PathBuf,
    output: PathBuf,
    steps: usize,
    seed: u64,
    lr: f64,
    model_config: ModelConfig,
) {
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

    print_config("text", &model_config);
    println!("num docs:    {}", dataset.docs.len());
    println!("vocab size:  {}", dataset.tokenizer.vocab_size);

    let model = TensorGpt::with_config(dataset.tokenizer.vocab_size, seed, model_config)
        .unwrap_or_else(|e| {
            eprintln!("error: failed to create model: {e}");
            process::exit(1);
        });

    let params = model.params();
    let num_params: usize = params.iter().map(|p| p.elem_count()).sum();
    println!("num params:  {}", num_params);
    println!();

    let config = TrainConfig {
        learning_rate: lr,
        num_steps: steps,
        ..TrainConfig::default()
    };

    let mut adam = TensorAdam::new(params, config).unwrap_or_else(|e| {
        eprintln!("error: failed to create optimizer: {e}");
        process::exit(1);
    });

    let train_start = Instant::now();

    for step in 0..steps {
        let doc = &dataset.docs[step % dataset.docs.len()];
        let tokens_vec = dataset.tokenizer.encode_doc(doc);

        let tokens = Tensor::from_vec(
            tokens_vec.iter().map(|&x| x as u32).collect::<Vec<_>>(),
            (1, tokens_vec.len()),
            &model.device
        ).unwrap(); // Handle error?

        let loss = tensor_train_step(&model, &tokens, &mut adam, step).unwrap_or_else(|e| {
            eprintln!("error during training step {}: {}", step, e);
            process::exit(1);
        });

        let elapsed = train_start.elapsed().as_secs_f64();
        let avg = elapsed / (step + 1) as f64;
        let eta = avg * (steps - step - 1) as f64;
        println!(
            "step {:4} / {:4} | loss {:.4} | {:.1}s/step | eta {}",
            step + 1,
            steps,
            loss,
            avg,
            format_eta(eta),
        );
    }

    let total = train_start.elapsed().as_secs_f64();
    println!("\ntraining complete in {}", format_eta(total));

    save_tensor_model(&model, &dataset.tokenizer, &output);

    // Show a few samples using InferenceGpt
    println!("\n--- samples ---");
    let weights_json = model.save_weights().unwrap();
    let inf_model = InferenceGpt::load_weights_with_config(
        dataset.tokenizer.vocab_size,
        &weights_json,
        model_config
    ).unwrap();

    let tok = &dataset.tokenizer;
    for i in 0..5 {
        let name = inf_model.generate(tok.bos, 0.5, seed + i, |id| tok.decode(id));
        println!("  {name}");
    }
}

fn run_train_chat(
    input: PathBuf,
    output: PathBuf,
    steps: usize,
    seed: u64,
    lr: f64,
    model_config: ModelConfig,
) {
    if !input.exists() {
        eprintln!("error: input file not found: {}", input.display());
        process::exit(1);
    }
    if let Err(e) = fs::create_dir_all(&output) {
        eprintln!("error: cannot create output directory: {e}");
        process::exit(1);
    }

    let dataset = ChatDataset::load(&input).unwrap_or_else(|e| {
        eprintln!("error: {e}");
        process::exit(1);
    });

    print_config("chat", &model_config);
    println!("num convos:  {}", dataset.len());
    println!("vocab size:  {}", dataset.tokenizer.vocab_size);

    let model = TensorGpt::with_config(dataset.tokenizer.vocab_size, seed, model_config)
        .unwrap_or_else(|e| {
            eprintln!("error: failed to create model: {e}");
            process::exit(1);
        });

    let params = model.params();
    let num_params: usize = params.iter().map(|p| p.elem_count()).sum();
    println!("num params:  {}", num_params);
    println!();

    let config = TrainConfig {
        learning_rate: lr,
        num_steps: steps,
        ..TrainConfig::default()
    };

    let mut adam = TensorAdam::new(params, config).unwrap();
    let train_start = Instant::now();

    for step in 0..steps {
        let tokens_vec = dataset.encode_conversation(step % dataset.len());

        let tokens = Tensor::from_vec(
            tokens_vec.iter().map(|&x| x as u32).collect::<Vec<_>>(),
            (1, tokens_vec.len()),
            &model.device
        ).unwrap();

        let loss = tensor_train_step(&model, &tokens, &mut adam, step).unwrap();

        let elapsed = train_start.elapsed().as_secs_f64();
        let avg = elapsed / (step + 1) as f64;
        let eta = avg * (steps - step - 1) as f64;
        println!(
            "step {:4} / {:4} | loss {:.4} | {:.1}s/step | eta {}",
            step + 1,
            steps,
            loss,
            avg,
            format_eta(eta),
        );
    }

    let total = train_start.elapsed().as_secs_f64();
    println!("\ntraining complete in {}", format_eta(total));

    save_tensor_model(&model, &dataset.tokenizer, &output);
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

fn print_config(mode: &str, config: &ModelConfig) {
    println!("microgpt train ({mode})");
    println!("  n_embd:      {}", config.n_embd);
    println!("  n_head:      {}", config.n_head);
    println!("  n_layer:     {}", config.n_layer);
    println!("  block_size:  {}", config.block_size);
}

fn run_generate(model_dir: PathBuf, num_samples: usize, temperature: f64, seed: u64) {
    let (tokenizer, model) = load_inference_model(&model_dir);

    for i in 0..num_samples {
        let sample = model.generate(
            tokenizer.bos,
            temperature,
            seed + i as u64,
            |id| tokenizer.decode(id)
        );
        println!("sample {:2}: {sample}", i + 1);
    }
}

fn run_chat(model_dir: PathBuf, temperature: f64, seed: u64) {
    let (tokenizer, model) = load_inference_model(&model_dir);

    let special = match &tokenizer.special_tokens {
        Some(s) => s.clone(),
        None => {
            eprintln!("error: this model was not trained with chat tokens");
            eprintln!("hint: retrain with --chat to enable chat support");
            process::exit(1);
        }
    };

    println!("microgpt chat (block_size={})", model.config.block_size);
    println!("type /quit to exit, /clear to reset history\n");

    let mut rl = rustyline::DefaultEditor::new().unwrap_or_else(|e| {
        eprintln!("error: failed to initialize readline: {e}");
        process::exit(1);
    });

    let mut history: Vec<usize> = Vec::new();
    let mut turn_count: u64 = 0;

    loop {
        let input = match rl.readline("you> ") {
            Ok(line) => line,
            Err(
                rustyline::error::ReadlineError::Interrupted
                | rustyline::error::ReadlineError::Eof,
            ) => break,
            Err(e) => {
                eprintln!("error: {e}");
                break;
            }
        };

        let input = input.trim();
        if input.is_empty() {
            continue;
        }

        match input {
            "/quit" => break,
            "/clear" => {
                history.clear();
                turn_count = 0;
                println!("(history cleared)");
                continue;
            }
            _ => {}
        }

        let _ = rl.add_history_entry(input);

        // Encode user turn and append to history.
        history.extend(tokenizer.encode_turn("user", input));

        // Append the assistant role token to prompt the model.
        history.push(special.assistant);

        // Truncate history from the front if it exceeds block_size,
        // leaving room for at least one generated token.
        let max_prompt = model.config.block_size.saturating_sub(1);
        if history.len() > max_prompt {
            let excess = history.len() - max_prompt;
            history.drain(..excess);
        }

        // Generate response.
        print!("microgpt> ");
        let rng_seed = seed.wrapping_add(turn_count);

        let output = model.generate_from_prompt(
            &history,
            special.end_turn,
            temperature,
            rng_seed,
            |tok| {
                if let Some(ch) = tokenizer.decode(tok) {
                    print!("{ch}");
                    io::stdout().flush().ok();
                }
            },
        );
        println!();

        // Append assistant response + end_turn to history for next turn.
        history.extend(&output);
        history.push(special.end_turn);

        turn_count += 1;
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
    println!("  vocab_size:      {}", meta.vocab_size);
    println!("  n_embd:          {}", meta.n_embd);
    println!("  n_head:          {}", meta.n_head);
    println!("  n_layer:         {}", meta.n_layer);
    println!("  block_size:      {}", meta.block_size);
    println!("  charset:         {:?}", meta.chars);
    if let Some(ref st) = meta.special_tokens {
        println!("  special_tokens:  {:?}", st);
    }

    let weights_path = model_dir.join("weights.json");
    if weights_path.exists() {
        let weights_json = fs::read_to_string(&weights_path).unwrap_or_else(|e| {
            eprintln!("error: cannot read weights: {e}");
            process::exit(1);
        });
        let config = meta.config();
        let model = InferenceGpt::load_weights_with_config(meta.vocab_size, &weights_json, config)
            .unwrap_or_else(|e| {
                eprintln!("error: {e}");
                process::exit(1);
            });
        println!("  num_params:      {}", model.num_params());
    }
}

fn save_tensor_model(model: &TensorGpt, tokenizer: &Tokenizer, output: &PathBuf) {
    let weights_json = model.save_weights().unwrap();
    let weights_path = output.join("weights.json");
    if let Err(e) = fs::write(&weights_path, &weights_json) {
        eprintln!("error: failed to save weights: {e}");
        process::exit(1);
    }
    println!("\nweights saved to {}", weights_path.display());

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

fn load_model(model_dir: &PathBuf) -> (InferenceGpt, Tokenizer) {
    let (meta, weights_json) = load_meta_and_weights(model_dir);
    let config = meta.config();
    let model = InferenceGpt::load_weights_with_config(meta.vocab_size, &weights_json, config)
        .unwrap_or_else(|e| {
            eprintln!("error: {e}");
            process::exit(1);
        });
    let tokenizer = Tokenizer::from_meta(meta.chars, meta.special_tokens.as_deref());
    (model, tokenizer)
}

fn load_inference_model(model_dir: &PathBuf) -> (Tokenizer, InferenceGpt) {
    let (model, tokenizer) = load_model(model_dir);
    (tokenizer, model)
}

fn load_meta_and_weights(model_dir: &PathBuf) -> (ModelMeta, String) {
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

    (meta, weights_json)
}
