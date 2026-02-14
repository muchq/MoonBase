use std::env;
use std::fs;
use std::path::PathBuf;
use std::process;

use microgpt::model::ModelMeta;
use microgpt::{Adam, ChatDataset, Dataset, Gpt, ModelConfig, TrainConfig, generate, train_step};

fn main() {
    tracing_subscriber::fmt::init();

    let input_path = env::var("TRAIN_INPUT")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from("input.txt"));

    let output_dir = env::var("TRAIN_OUTPUT_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from("output"));

    let num_steps: usize = env::var("TRAIN_STEPS")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(1000);

    let seed: u64 = env::var("TRAIN_SEED")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(42);

    let lr: f64 = env::var("TRAIN_LR")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(0.01);

    let chat_mode = env::var("TRAIN_CHAT").is_ok();

    let n_embd: usize = env::var("TRAIN_N_EMBD")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(16);

    let n_head: usize = env::var("TRAIN_N_HEAD")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(4);

    let n_layer: usize = env::var("TRAIN_N_LAYER")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(1);

    let block_size: usize = env::var("TRAIN_BLOCK_SIZE")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(16);

    if n_embd % n_head != 0 {
        eprintln!("error: TRAIN_N_EMBD ({n_embd}) must be divisible by TRAIN_N_HEAD ({n_head})");
        process::exit(1);
    }

    let model_config = ModelConfig {
        n_embd,
        n_head,
        n_layer,
        block_size,
    };

    if !input_path.exists() {
        eprintln!("error: training data not found at {}", input_path.display());
        eprintln!("set TRAIN_INPUT to the path of a newline-delimited text file");
        process::exit(1);
    }

    if let Err(e) = fs::create_dir_all(&output_dir) {
        eprintln!("error: cannot create output directory: {e}");
        process::exit(1);
    }

    let mode_str = if chat_mode { "chat" } else { "text" };
    println!("microgpt-train ({mode_str})");
    println!("  input:      {}", input_path.display());
    println!("  output:     {}", output_dir.display());
    println!("  steps:      {num_steps}");
    println!("  seed:       {seed}");
    println!("  lr:         {lr}");
    println!("  n_embd:     {n_embd}");
    println!("  n_head:     {n_head}");
    println!("  n_layer:    {n_layer}");
    println!("  block_size: {block_size}");
    println!();

    if chat_mode {
        run_train_chat(&input_path, &output_dir, num_steps, seed, lr, model_config);
    } else {
        run_train_text(&input_path, &output_dir, num_steps, seed, lr, model_config);
    }
}

fn run_train_text(
    input_path: &PathBuf,
    output_dir: &PathBuf,
    num_steps: usize,
    seed: u64,
    lr: f64,
    model_config: ModelConfig,
) {
    let dataset = Dataset::load(input_path).unwrap_or_else(|e| {
        eprintln!("error: {e}");
        process::exit(1);
    });

    println!("num docs:    {}", dataset.docs.len());
    println!("vocab size:  {}", dataset.tokenizer.vocab_size);

    let model = Gpt::with_config(dataset.tokenizer.vocab_size, seed, model_config);
    let params = model.params();
    println!("num params:  {}", params.len());
    println!();

    let config = TrainConfig {
        learning_rate: lr,
        num_steps,
        ..TrainConfig::default()
    };
    let mut adam = Adam::new(params.len());

    for step in 0..num_steps {
        let doc = &dataset.docs[step % dataset.docs.len()];
        let tokens = dataset.tokenizer.encode_doc(doc);
        let loss = train_step(&model, &tokens, &params, &mut adam, &config, step);
        println!("step {:4} / {:4} | loss {:.4}", step + 1, num_steps, loss);
    }

    save_model(&model, &dataset.tokenizer, output_dir);

    // Generate samples
    println!("\n--- samples ---");
    let tok = &dataset.tokenizer;
    for i in 0..5 {
        let name = generate(&model, tok.bos, 0.5, seed + i, |id| tok.decode(id));
        println!("  {name}");
    }
}

fn run_train_chat(
    input_path: &PathBuf,
    output_dir: &PathBuf,
    num_steps: usize,
    seed: u64,
    lr: f64,
    model_config: ModelConfig,
) {
    let dataset = ChatDataset::load(input_path).unwrap_or_else(|e| {
        eprintln!("error: {e}");
        process::exit(1);
    });

    println!("num convos:  {}", dataset.len());
    println!("vocab size:  {}", dataset.tokenizer.vocab_size);

    let model = Gpt::with_config(dataset.tokenizer.vocab_size, seed, model_config);
    let params = model.params();
    println!("num params:  {}", params.len());
    println!();

    let config = TrainConfig {
        learning_rate: lr,
        num_steps,
        ..TrainConfig::default()
    };
    let mut adam = Adam::new(params.len());

    for step in 0..num_steps {
        let tokens = dataset.encode_conversation(step % dataset.len());
        let loss = train_step(&model, &tokens, &params, &mut adam, &config, step);
        println!("step {:4} / {:4} | loss {:.4}", step + 1, num_steps, loss);
    }

    save_model(&model, &dataset.tokenizer, output_dir);
}

fn save_model(model: &Gpt, tokenizer: &microgpt::Tokenizer, output_dir: &PathBuf) {
    let weights_json = model.save_weights();
    let weights_path = output_dir.join("weights.json");
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
    let meta_path = output_dir.join("meta.json");
    if let Err(e) = fs::write(&meta_path, &meta_json) {
        eprintln!("error: failed to save metadata: {e}");
        process::exit(1);
    }
    println!("metadata saved to {}", meta_path.display());
}
