use std::sync::Arc;
use std::time::Instant;

use axum::Json;
use axum::extract::State;
use axum::http::StatusCode;
use axum::response::IntoResponse;

use crate::AppState;
use crate::types::{
    ChatRequest, ChatResponse, ErrorResponse, GenerateRequest, GenerateResponse,
};

fn error_response(status: StatusCode, msg: String) -> impl IntoResponse {
    (
        status,
        Json(serde_json::to_value(ErrorResponse { error: msg }).unwrap()),
    )
}

pub async fn generate_post(
    State(state): State<Arc<AppState>>,
    Json(req): Json<GenerateRequest>,
) -> impl IntoResponse {
    if let Err(msg) = req.validate() {
        return error_response(StatusCode::BAD_REQUEST, msg).into_response();
    }

    let num = req.num_samples.min(50);
    let tok = &state.tokenizer;

    let start = Instant::now();
    let samples: Vec<String> = (0..num)
        .map(|i| {
            state.model.generate(
                tok.bos,
                req.temperature,
                req.seed + i as u64,
                req.max_tokens,
                |id| tok.decode(id),
            )
        })
        .collect();
    let duration_ms = start.elapsed().as_secs_f64() * 1000.0;

    // Approximate token count: 1 token ≈ 4 chars is a common heuristic.
    let approx_tokens = samples.iter().map(|s| (s.len() / 4).max(1)).sum::<usize>() as u64;
    state.metrics.record_generate(approx_tokens, duration_ms);

    Json(GenerateResponse { samples }).into_response()
}

pub async fn chat_post(
    State(state): State<Arc<AppState>>,
    Json(req): Json<ChatRequest>,
) -> impl IntoResponse {
    let tok = &state.tokenizer;

    let special = match &tok.special_tokens {
        Some(s) => s,
        None => {
            return error_response(
                StatusCode::BAD_REQUEST,
                "model was not trained with chat tokens".to_string(),
            )
            .into_response();
        }
    };

    if let Err(msg) = req.validate() {
        return error_response(StatusCode::BAD_REQUEST, msg).into_response();
    }

    let turns: Vec<(&str, &str)> = req
        .messages
        .iter()
        .map(|m| (m.role.as_str(), m.content.as_str()))
        .collect();
    let mut prompt_tokens = tok.encode_conversation(&turns);

    prompt_tokens.push(special.assistant);

    let tokens_dropped =
        tok.truncate_chat_prompt(&mut prompt_tokens, state.model.config.block_size);
    if tokens_dropped > 0 && prompt_tokens.first() != Some(&tok.bos) {
        prompt_tokens.insert(0, tok.bos);
    }

    let remaining = state.model.config.block_size.saturating_sub(prompt_tokens.len());
    let default_max = remaining.max(64).min(state.model.config.block_size / 4);
    let max_tokens = Some(req.max_tokens.unwrap_or(default_max));
    let stop_tokens = [special.end_turn];
    let suppress_tokens = [tok.bos, special.user, special.assistant];

    let start = Instant::now();
    let output_tokens = state.model.generate_from_prompt(
        &prompt_tokens,
        &stop_tokens,
        &suppress_tokens,
        req.temperature,
        req.seed,
        max_tokens,
        |_| {},
    );
    let duration_ms = start.elapsed().as_secs_f64() * 1000.0;

    state.metrics.record_chat(output_tokens.len() as u64, duration_ms);

    let content = tok.decode_str(&output_tokens)
        .trim()
        .trim_start_matches(|c: char| c.is_ascii_punctuation())
        .trim_start()
        .to_string();

    Json(serde_json::to_value(ChatResponse {
        role: "assistant".to_string(),
        content,
        tokens_dropped,
    })
    .unwrap())
    .into_response()
}

#[cfg(test)]
mod tests {
    use super::*;
    use axum::Router;
    use axum::body::Body;
    use axum::http::Request;
    use axum::routing::post;
    use http_body_util::BodyExt;
    use microgpt::{DType, Device, InferenceGpt, ModelConfig, TensorGpt, Tokenizer};
    use tower::ServiceExt;

    const TEST_CORPUS: &str = "Hello world! This is a test of the BPE tokenizer. \
        It should handle punctuation, numbers like 42, and various words properly. \
        The quick brown fox jumps over the lazy dog repeatedly.";

    fn make_test_state(chat: bool) -> Arc<AppState> {
        let tok = Tokenizer::train_bpe(TEST_CORPUS, 500, chat);
        let config = ModelConfig {
            n_embd: 16,
            n_head: 2,
            n_layer: 1,
            block_size: 64,
        };
        let device = Device::Cpu;
        let model = TensorGpt::new(tok.vocab_size, 42, config, &device, DType::F32);
        let bytes = model.save_weights_st().unwrap();
        let model = InferenceGpt::load_safetensors(tok.vocab_size, &bytes, config).unwrap();
        Arc::new(AppState { model, tokenizer: tok })
    }

    fn test_app(state: Arc<AppState>) -> Router {
        Router::new()
            .route("/generate", post(generate_post))
            .route("/chat", post(chat_post))
            .with_state(state)
    }

    fn json_request(uri: &str, body: &str) -> Request<Body> {
        Request::builder()
            .uri(uri)
            .method("POST")
            .header("content-type", "application/json")
            .body(Body::from(body.to_string()))
            .unwrap()
    }

    async fn response_body(resp: axum::response::Response) -> serde_json::Value {
        let bytes = resp.into_body().collect().await.unwrap().to_bytes();
        serde_json::from_slice(&bytes).unwrap()
    }

    // ---- /generate handler tests ----

    #[tokio::test]
    async fn generate_returns_samples() {
        let state = make_test_state(false);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request("/generate", r#"{"num_samples":2}"#))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::OK);
        let body = response_body(resp).await;
        assert_eq!(body["samples"].as_array().unwrap().len(), 2);
    }

    #[tokio::test]
    async fn generate_with_defaults() {
        let state = make_test_state(false);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request("/generate", "{}"))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::OK);
        let body = response_body(resp).await;
        assert_eq!(body["samples"].as_array().unwrap().len(), 1);
    }

    #[tokio::test]
    async fn generate_caps_num_samples_at_50() {
        let state = make_test_state(false);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request("/generate", r#"{"num_samples":100}"#))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::OK);
        let body = response_body(resp).await;
        assert_eq!(body["samples"].as_array().unwrap().len(), 50);
    }

    #[tokio::test]
    async fn generate_deterministic_with_same_seed() {
        let state = make_test_state(false);

        let app1 = test_app(state.clone());
        let resp1 = app1
            .oneshot(json_request(
                "/generate",
                r#"{"seed":123,"temperature":0.5}"#,
            ))
            .await
            .unwrap();
        let body1 = response_body(resp1).await;

        let app2 = test_app(state);
        let resp2 = app2
            .oneshot(json_request(
                "/generate",
                r#"{"seed":123,"temperature":0.5}"#,
            ))
            .await
            .unwrap();
        let body2 = response_body(resp2).await;

        assert_eq!(body1["samples"], body2["samples"]);
    }

    #[tokio::test]
    async fn generate_rejects_negative_temperature() {
        let state = make_test_state(false);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request("/generate", r#"{"temperature":-1}"#))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
        let body = response_body(resp).await;
        assert!(body["error"].as_str().unwrap().contains("temperature"));
    }

    #[tokio::test]
    async fn generate_rejects_zero_num_samples() {
        let state = make_test_state(false);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request("/generate", r#"{"num_samples":0}"#))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
        let body = response_body(resp).await;
        assert!(body["error"].as_str().unwrap().contains("num_samples"));
    }

    #[tokio::test]
    async fn generate_rejects_zero_max_tokens() {
        let state = make_test_state(false);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request("/generate", r#"{"max_tokens":0}"#))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
        let body = response_body(resp).await;
        assert!(body["error"].as_str().unwrap().contains("max_tokens"));
    }

    #[tokio::test]
    async fn generate_rejects_malformed_json() {
        let state = make_test_state(false);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request("/generate", "not json"))
            .await
            .unwrap();
        assert!(
            resp.status() == StatusCode::BAD_REQUEST
                || resp.status() == StatusCode::UNPROCESSABLE_ENTITY,
            "expected 400 or 422, got {}",
            resp.status()
        );
    }

    #[tokio::test]
    async fn generate_with_max_tokens() {
        let state = make_test_state(false);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request("/generate", r#"{"max_tokens":5}"#))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::OK);
        let body = response_body(resp).await;
        assert!(!body["samples"].as_array().unwrap().is_empty());
    }

    // ---- /chat handler tests ----

    #[tokio::test]
    async fn chat_returns_assistant_response() {
        let state = make_test_state(true);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request(
                "/chat",
                r#"{"messages":[{"role":"user","content":"hello"}]}"#,
            ))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::OK);
        let body = response_body(resp).await;
        assert_eq!(body["role"], "assistant");
        assert!(body["content"].is_string());
        assert!(body["tokens_dropped"].is_number());
    }

    #[tokio::test]
    async fn chat_rejects_empty_messages() {
        let state = make_test_state(true);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request("/chat", r#"{"messages":[]}"#))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
        let body = response_body(resp).await;
        assert!(body["error"].as_str().unwrap().contains("empty"));
    }

    #[tokio::test]
    async fn chat_rejects_unknown_role() {
        let state = make_test_state(true);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request(
                "/chat",
                r#"{"messages":[{"role":"system","content":"you are helpful"}]}"#,
            ))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
        let body = response_body(resp).await;
        assert!(body["error"].as_str().unwrap().contains("unknown role"));
    }

    #[tokio::test]
    async fn chat_rejects_empty_content() {
        let state = make_test_state(true);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request(
                "/chat",
                r#"{"messages":[{"role":"user","content":""}]}"#,
            ))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
        let body = response_body(resp).await;
        assert!(body["error"].as_str().unwrap().contains("content"));
    }

    #[tokio::test]
    async fn chat_rejects_negative_temperature() {
        let state = make_test_state(true);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request(
                "/chat",
                r#"{"messages":[{"role":"user","content":"hi"}],"temperature":-0.5}"#,
            ))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
        let body = response_body(resp).await;
        assert!(body["error"].as_str().unwrap().contains("temperature"));
    }

    #[tokio::test]
    async fn chat_rejects_zero_max_tokens() {
        let state = make_test_state(true);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request(
                "/chat",
                r#"{"messages":[{"role":"user","content":"hi"}],"max_tokens":0}"#,
            ))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
    }

    #[tokio::test]
    async fn chat_rejects_without_chat_tokens() {
        let state = make_test_state(false);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request(
                "/chat",
                r#"{"messages":[{"role":"user","content":"hello"}]}"#,
            ))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
        let body = response_body(resp).await;
        assert!(body["error"].as_str().unwrap().contains("chat tokens"));
    }

    #[tokio::test]
    async fn chat_rejects_malformed_json() {
        let state = make_test_state(true);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request("/chat", "{bad json}"))
            .await
            .unwrap();
        assert!(
            resp.status() == StatusCode::BAD_REQUEST
                || resp.status() == StatusCode::UNPROCESSABLE_ENTITY,
            "expected 400 or 422, got {}",
            resp.status()
        );
    }

    #[tokio::test]
    async fn chat_multi_turn() {
        let state = make_test_state(true);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request(
                "/chat",
                r#"{"messages":[{"role":"user","content":"hi"},{"role":"assistant","content":"hello"},{"role":"user","content":"how are you"}]}"#,
            ))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::OK);
        let body = response_body(resp).await;
        assert_eq!(body["role"], "assistant");
    }

    #[tokio::test]
    async fn chat_deterministic_with_same_seed() {
        let state = make_test_state(true);
        let body_json = r#"{"messages":[{"role":"user","content":"hello"}],"seed":77,"temperature":0.5}"#;

        let app1 = test_app(state.clone());
        let resp1 = app1
            .oneshot(json_request("/chat", body_json))
            .await
            .unwrap();
        let body1 = response_body(resp1).await;

        let app2 = test_app(state);
        let resp2 = app2
            .oneshot(json_request("/chat", body_json))
            .await
            .unwrap();
        let body2 = response_body(resp2).await;

        assert_eq!(body1["content"], body2["content"]);
    }

    #[tokio::test]
    async fn chat_with_custom_max_tokens() {
        let state = make_test_state(true);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request(
                "/chat",
                r#"{"messages":[{"role":"user","content":"tell me a story"}],"max_tokens":3}"#,
            ))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::OK);
    }

    #[tokio::test]
    async fn chat_content_has_no_leading_punctuation() {
        let state = make_test_state(true);
        let app = test_app(state);
        let resp = app
            .oneshot(json_request(
                "/chat",
                r#"{"messages":[{"role":"user","content":"hello"}],"max_tokens":10}"#,
            ))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::OK);
        let body = response_body(resp).await;
        let content = body["content"].as_str().unwrap();
        if !content.is_empty() {
            let first = content.chars().next().unwrap();
            assert!(
                !first.is_ascii_punctuation(),
                "chat content should not start with punctuation, got: {content:?}"
            );
        }
    }

    #[tokio::test]
    async fn chat_long_context_returns_ok() {
        let state = make_test_state(true);
        let app = test_app(state);
        // Send many turns to force truncation
        let msgs: Vec<String> = (0..20)
            .flat_map(|i| {
                vec![
                    format!(r#"{{"role":"user","content":"message number {i}"}}"#),
                    format!(r#"{{"role":"assistant","content":"reply number {i}"}}"#),
                ]
            })
            .collect();
        let all_msgs = msgs.join(",");
        // Add a final user turn
        let body = format!(
            r#"{{"messages":[{all_msgs},{{"role":"user","content":"final question"}}],"max_tokens":5}}"#
        );
        let resp = app
            .oneshot(json_request("/chat", &body))
            .await
            .unwrap();
        assert_eq!(resp.status(), StatusCode::OK);
        let body = response_body(resp).await;
        assert!(body["tokens_dropped"].as_u64().unwrap() > 0, "long context should trigger truncation");
    }

    #[test]
    fn leading_punctuation_stripping() {
        assert_eq!(
            "! Hello".trim().trim_start_matches(|c: char| c.is_ascii_punctuation()).trim_start(),
            "Hello"
        );
        assert_eq!(
            "!!? test".trim().trim_start_matches(|c: char| c.is_ascii_punctuation()).trim_start(),
            "test"
        );
        assert_eq!(
            "Hello world".trim().trim_start_matches(|c: char| c.is_ascii_punctuation()).trim_start(),
            "Hello world"
        );
        assert_eq!(
            "".trim().trim_start_matches(|c: char| c.is_ascii_punctuation()).trim_start(),
            ""
        );
    }
}
