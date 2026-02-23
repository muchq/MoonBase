use std::fs;
use std::path::PathBuf;
use std::process;

use microgpt::{InferenceGpt, Tokenizer};

use crate::train::{load_meta, load_weights};

pub fn run_generate(model_dir: PathBuf, num_samples: usize, temperature: f64, seed: u64) {
    let (tokenizer, model) = load_inference_model(&model_dir);

    for i in 0..num_samples {
        let tok = &tokenizer;
        let sample = model.generate(tok.bos, temperature, seed + i as u64, None, |id| {
            tok.decode(id)
        });
        println!("sample {:2}: {sample}", i + 1);
    }
}

pub fn run_chat(model_dir: PathBuf, temperature: f64, seed: u64) {
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

    let mut history: Vec<usize> = vec![tokenizer.bos];
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
                history.push(tokenizer.bos);
                turn_count = 0;
                println!("(history cleared)");
                continue;
            }
            _ => {}
        }

        let _ = rl.add_history_entry(input);

        history.extend(tokenizer.encode_turn("user", input));
        history.push(special.assistant);

        let dropped = tokenizer.truncate_chat_prompt(&mut history, model.config.block_size);
        if dropped > 0 {
            // Re-prepend BOS so the model always sees a proper sequence start
            if history.first() != Some(&tokenizer.bos) {
                history.insert(0, tokenizer.bos);
            }
            println!("(context truncated, dropped {dropped} tokens of early history)");
        }

        let rng_seed = seed.wrapping_add(turn_count);
        let remaining = model.config.block_size.saturating_sub(history.len());
        let max_gen = remaining.max(64).min(model.config.block_size / 4);

        let mut raw_text = String::new();
        let stop_tokens = [special.end_turn];
        let suppress_tokens = [tokenizer.bos, special.user, special.assistant];
        let output = model.generate_from_prompt(
            &history,
            &stop_tokens,
            &suppress_tokens,
            temperature,
            rng_seed,
            Some(max_gen),
            |tok| {
                if let Some(s) = tokenizer.decode(tok) {
                    raw_text.push_str(&s);
                }
            },
        );
        let trimmed = raw_text.trim().trim_start_matches(|c: char| c.is_ascii_punctuation());
        println!("microgpt> {}", trimmed.trim_start());

        // Only add non-empty responses to history to avoid wasting context
        if !output.is_empty() {
            history.extend(&output);
            history.push(special.end_turn);
        } else {
            // Remove the dangling <assistant> token we added above
            history.pop();
        }

        turn_count += 1;
    }
}

pub fn run_info(model_dir: PathBuf) {
    let meta = load_meta(&model_dir);

    println!("microgpt model");
    println!("  vocab_size:      {}", meta.vocab_size);
    println!("  n_embd:          {}", meta.n_embd);
    println!("  n_head:          {}", meta.n_head);
    println!("  n_layer:         {}", meta.n_layer);
    println!("  block_size:      {}", meta.block_size);
    println!("  tokenizer:       BPE");
    if let Some(ref st) = meta.special_tokens {
        println!("  special_tokens:  {:?}", st);
    }

    let config = meta.config();
    let weights_bytes = load_weights(&model_dir);
    let model = InferenceGpt::load_safetensors(meta.vocab_size, &weights_bytes, config)
        .unwrap_or_else(|e| {
            eprintln!("error: {e}");
            process::exit(1);
        });
    println!("  num_params:      {}", model.num_params());

    let st_path = model_dir.join("weights.safetensors");
    let size = fs::metadata(&st_path).map(|m| m.len()).unwrap_or(0);
    println!("  format:          safetensors ({:.1} MB)", size as f64 / 1e6);
}

pub fn run_export(model_dir: PathBuf, output: PathBuf, half: bool) {
    let meta = load_meta(&model_dir);
    let config = meta.config();
    let weights_bytes = load_weights(&model_dir);
    let model = InferenceGpt::load_safetensors(meta.vocab_size, &weights_bytes, config)
        .unwrap_or_else(|e| {
            eprintln!("error: {e}");
            process::exit(1);
        });

    if let Err(e) = fs::create_dir_all(&output) {
        eprintln!("error: cannot create output directory: {e}");
        process::exit(1);
    }

    let dtype = if half {
        microgpt::StDtype::F16
    } else {
        microgpt::StDtype::F32
    };
    let bytes = microgpt::serialize_state_dict_st(&model.state_dict, dtype).unwrap_or_else(|e| {
        eprintln!("error: failed to serialize weights: {e}");
        process::exit(1);
    });
    let weights_path = output.join("weights.safetensors");
    if let Err(e) = fs::write(&weights_path, &bytes) {
        eprintln!("error: failed to write weights: {e}");
        process::exit(1);
    }

    let meta_json = serde_json::to_string_pretty(&meta).expect("meta serialization");
    if let Err(e) = fs::write(output.join("meta.json"), &meta_json) {
        eprintln!("error: failed to write meta.json: {e}");
        process::exit(1);
    }

    // Copy tokenizer.json from source model
    let src_tok = model_dir.join("tokenizer.json");
    if src_tok.exists() {
        let dst_tok = output.join("tokenizer.json");
        if let Err(e) = fs::copy(&src_tok, &dst_tok) {
            eprintln!("error: failed to copy tokenizer.json: {e}");
            process::exit(1);
        }
    }

    let dtype_name = if half { "f16" } else { "f32" };
    let size = fs::metadata(&weights_path).map(|m| m.len()).unwrap_or(0);
    println!(
        "exported {} model ({} params) to {} ({dtype_name}, {:.1} MB)",
        if half { "f16" } else { "f32" },
        model.num_params(),
        weights_path.display(),
        size as f64 / 1e6
    );
}

fn load_inference_model(model_dir: &std::path::Path) -> (Tokenizer, InferenceGpt) {
    let meta = load_meta(model_dir);
    let config = meta.config();
    let weights_bytes = load_weights(model_dir);
    let model = InferenceGpt::load_safetensors(meta.vocab_size, &weights_bytes, config)
        .unwrap_or_else(|e| {
            eprintln!("error: {e}");
            process::exit(1);
        });

    let tok_path = model_dir.join("tokenizer.json");
    let tokenizer = Tokenizer::from_file(&tok_path).unwrap_or_else(|e| {
        eprintln!("error: {e}");
        process::exit(1);
    });

    (tokenizer, model)
}
