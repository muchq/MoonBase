mod service;
mod types;

use std::env;
use std::fs;
use std::path::PathBuf;
use std::process;
use std::sync::Arc;

use axum::routing::post;
use microgpt::model::ModelMeta;
use microgpt::{InferenceGpt, Tokenizer};
use server_pal::{RateLimit, listen_addr_pal, router_builder, serve};
use tracing::{Level, event};

use crate::service::{chat_post, generate_post};

pub struct AppState {
    pub model: InferenceGpt,
    pub tokenizer: Tokenizer,
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt::init();

    let model_dir = env::var("MODEL_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from("output"));

    let weights_path = model_dir.join("weights.json");
    let meta_path = model_dir.join("meta.json");

    let meta_json = fs::read_to_string(&meta_path).unwrap_or_else(|e| {
        eprintln!(
            "error: cannot read {}: {e}\nSet MODEL_DIR to the directory containing weights.json and meta.json",
            meta_path.display()
        );
        process::exit(1);
    });
    let meta: ModelMeta = serde_json::from_str(&meta_json).unwrap_or_else(|e| {
        eprintln!("error: invalid meta.json: {e}");
        process::exit(1);
    });

    let weights_json = fs::read_to_string(&weights_path).unwrap_or_else(|e| {
        eprintln!("error: cannot read {}: {e}", weights_path.display());
        process::exit(1);
    });
    let config = meta.config();
    let model = InferenceGpt::load_weights_with_config(meta.vocab_size, &weights_json, config)
        .unwrap_or_else(|e| {
            eprintln!("error: {e}");
            process::exit(1);
        });

    let tokenizer = Tokenizer::from_meta(meta.chars, meta.special_tokens.as_deref());

    event!(
        Level::INFO,
        "loaded model: vocab_size={}, params={}",
        tokenizer.vocab_size,
        model.num_params()
    );

    let state = Arc::new(AppState { model, tokenizer });

    let listen_address = listen_addr_pal();
    let has_chat = state.tokenizer.special_tokens.is_some();
    event!(Level::INFO, "chat support: {}", has_chat);

    let app = router_builder()
        .route("/microgpt/v1/generate", post(generate_post))
        .route("/microgpt/v1/chat", post(chat_post))
        .rate_limit(Some(RateLimit { per_second: 5, burst: 10 }))
        .build()
        .with_state(state);

    event!(Level::INFO, "listening on {}", listen_address);
    serve(app, &listen_address).await;
}
