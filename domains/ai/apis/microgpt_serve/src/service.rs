use std::sync::Arc;

use axum::Json;
use axum::extract::State;

use crate::AppState;
use crate::types::{GenerateRequest, GenerateResponse};

pub async fn generate_post(
    State(state): State<Arc<AppState>>,
    Json(req): Json<GenerateRequest>,
) -> Json<GenerateResponse> {
    let num = req.num_samples.min(50);
    let tok = &state.tokenizer;

    let samples: Vec<String> = (0..num)
        .map(|i| {
            state
                .model
                .generate(tok.bos, req.temperature, req.seed + i as u64, |id| {
                    tok.decode(id)
                })
        })
        .collect();

    Json(GenerateResponse { samples })
}
