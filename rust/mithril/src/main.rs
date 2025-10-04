use axum::extract::State;
use axum::{extract::Json as ExtractJson, response::Json, routing::post};
use serde::{Deserialize, Deserializer, Serialize};
use server_pal::{listen_addr_pal, router_builder};
use std::sync::Arc;
use tracing::{Level, event};
use tracing_subscriber;
use wordchains::{Graph, bfs_for_target, initialize_graph};

fn validate_word<'de, D>(deserializer: D) -> Result<String, D::Error>
where
    D: Deserializer<'de>,
{
    let word = String::deserialize(deserializer)?;

    if word.is_empty() {
        return Err(serde::de::Error::custom("word cannot be empty"));
    }

    if word.len() < 3 {
        return Err(serde::de::Error::custom("word must be at least 3 chars"));
    }

    if word.len() > 9 {
        return Err(serde::de::Error::custom("word must be at most 9 chars"));
    }

    if !word.chars().all(|c| c.is_alphabetic()) {
        return Err(serde::de::Error::custom("word must contain only letters"));
    }

    Ok(word)
}

#[derive(Deserialize)]
struct WordchainRequest {
    #[serde(deserialize_with = "validate_word")]
    start: String,
    #[serde(deserialize_with = "validate_word")]
    end: String,
}

#[derive(Serialize)]
struct WordchainResponse {
    path: Option<Vec<String>>,
}

struct AppState {
    word_graph: Graph,
}

async fn wordchain_post(
    State(state): State<Arc<AppState>>,
    ExtractJson(request): ExtractJson<WordchainRequest>,
) -> Json<WordchainResponse> {
    let result = bfs_for_target(request.start, request.end.as_str(), &state.word_graph);
    let response = WordchainResponse { path: result };
    Json(response)
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt::init();

    let data_dir = "mithril.runfiles/_main/rust/mithril/data";
    let path = format!("{}/words.txt", data_dir);
    let word_graph = initialize_graph(&path, Some(data_dir));

    let shared_state = Arc::new(AppState {
        word_graph: word_graph,
    });

    let listen_address = listen_addr_pal();

    // build our application with a single route
    let app = router_builder()
        .route("/mithril/v1/wordchain", post(wordchain_post))
        .build()
        .with_state(shared_state);

    let listener = tokio::net::TcpListener::bind(listen_address.clone())
        .await
        .unwrap();
    event!(Level::INFO, "listening on {}", listen_address);
    axum::serve(listener, app).await.unwrap();
}
