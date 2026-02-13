use std::env;
use std::fs;
use std::path::PathBuf;
use std::process;

use microgpt::{Adam, Dataset, Gpt, TrainConfig, generate, train_step};
use microgpt::model::ModelMeta;

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

    if !input_path.exists() {
        eprintln!("error: training data not found at {}", input_path.display());
        eprintln!("set TRAIN_INPUT to the path of a newline-delimited text file");
        process::exit(1);
    }

    if let Err(e) = fs::create_dir_all(&output_dir) {
        eprintln!("error: cannot create output directory: {e}");
        process::exit(1);
    }

    println!("microgpt-train");
    println!("  input:  {}", input_path.display());
    println!("  output: {}", output_dir.display());
    println!("  steps:  {num_steps}");
    println!("  seed:   {seed}");
    println!();

    let dataset = Dataset::load(&input_path).unwrap_or_else(|e| {
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

    // Save weights and metadata
    let weights_json = model.save_weights();
    let weights_path = output_dir.join("weights.json");
    if let Err(e) = fs::write(&weights_path, &weights_json) {
        eprintln!("error: failed to save weights: {e}");
        process::exit(1);
    }
    println!("\nweights saved to {}", weights_path.display());

    let meta = ModelMeta {
        vocab_size: dataset.tokenizer.vocab_size,
        n_embd: microgpt::N_EMBD,
        n_head: microgpt::N_HEAD,
        n_layer: microgpt::N_LAYER,
        block_size: microgpt::BLOCK_SIZE,
        chars: dataset.tokenizer.chars.clone(),
    };
    let meta_json = serde_json::to_string_pretty(&meta).expect("meta serialization");
    let meta_path = output_dir.join("meta.json");
    if let Err(e) = fs::write(&meta_path, &meta_json) {
        eprintln!("error: failed to save metadata: {e}");
        process::exit(1);
    }
    println!("metadata saved to {}", meta_path.display());

    // Generate samples
    println!("\n--- inference (new, hallucinated names) ---");
    let temperature = 0.5;
    let tok = &dataset.tokenizer;
    for i in 0..20 {
        let name = generate(&model, tok.bos, temperature, seed + i as u64, |id| {
            tok.decode(id)
        });
        println!("sample {:2}: {name}", i + 1);
    }
}
