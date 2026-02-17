use std::sync::Arc;

use axum::Json;
use axum::extract::State;
use axum::http::StatusCode;
use axum::response::IntoResponse;

use crate::AppState;
use crate::types::{
    ChatRequest, ChatResponse, ErrorResponse, GenerateRequest, GenerateResponse,
};

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

pub async fn chat_post(
    State(state): State<Arc<AppState>>,
    Json(req): Json<ChatRequest>,
) -> impl IntoResponse {
    let tok = &state.tokenizer;

    // Verify the model has chat tokens.
    let special = match &tok.special_tokens {
        Some(s) => s,
        None => {
            return (
                StatusCode::BAD_REQUEST,
                Json(serde_json::to_value(ErrorResponse {
                    error: "model was not trained with chat tokens".to_string(),
                })
                .unwrap()),
            )
                .into_response();
        }
    };

    if req.messages.is_empty() {
        return (
            StatusCode::BAD_REQUEST,
            Json(
                serde_json::to_value(ErrorResponse {
                    error: "messages must not be empty".to_string(),
                })
                .unwrap(),
            ),
        )
            .into_response();
    }

    // Validate roles.
    for msg in &req.messages {
        if msg.role != "user" && msg.role != "assistant" {
            return (
                StatusCode::BAD_REQUEST,
                Json(
                    serde_json::to_value(ErrorResponse {
                        error: format!("unknown role: {}", msg.role),
                    })
                    .unwrap(),
                ),
            )
                .into_response();
        }
    }

    // Encode the conversation as a token sequence.
    let turns: Vec<(&str, &str)> = req
        .messages
        .iter()
        .map(|m| (m.role.as_str(), m.content.as_str()))
        .collect();
    let mut prompt_tokens = tok.encode_conversation(&turns);

    // Append assistant role token to prompt the model.
    prompt_tokens.push(special.assistant);

    // Truncate prompt to fit within block_size, reserving room for generation.
    tok.truncate_chat_prompt(&mut prompt_tokens, state.model.config.block_size);

    // Generate response.
    let output_tokens = state.model.generate_from_prompt(
        &prompt_tokens,
        special.end_turn,
        req.temperature,
        req.seed,
        |_| {},
    );

    let content = tok.decode_str(&output_tokens);

    Json(serde_json::to_value(ChatResponse {
        role: "assistant".to_string(),
        content,
    })
    .unwrap())
    .into_response()
}
