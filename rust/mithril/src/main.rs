use axum::{
    routing::get,
    routing::post,
    response::Json,
    extract::Json as ExtractJson,
    Router,
};
use serde::{Deserialize, Serialize, Deserializer};
use std::env;
use std::sync::Arc;
use axum::extract::State;
use tracing::{event, Level};
use tracing_subscriber;
use wordchains::{bfs_for_target, initialize_graph, Graph};

const DEFAULT_PORT: u16 = 8080;

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
    word_graph: Graph
}

async fn wordchain_post(
    State(state): State<Arc<AppState>>,
    ExtractJson(request): ExtractJson<WordchainRequest>) -> Json<WordchainResponse> {
    let result = bfs_for_target(request.start, request.end.as_str(), &state.word_graph);
    let response = WordchainResponse { path: result };
    Json(response)
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt::init();

    let path = "rust/mithril/data/words.txt";
    let word_graph = initialize_graph(path, Some("rust/mithril/data"));

    let shared_state = Arc::new(AppState { word_graph: word_graph });

    let port = env::var("PORT")
        .ok()
        .and_then(|p| p.parse::<u16>().ok())
        .unwrap_or(DEFAULT_PORT);


    let listen_address = format!("0.0.0.0:{}", &port);

    // build our application with a single route
    let app = Router::new()
        .route("/health", get(|| async { "Ok" }))
        .route("/v1/wordchain", post(wordchain_post))
        .with_state(shared_state);

    let listener = tokio::net::TcpListener::bind(listen_address.clone()).await.unwrap();
    event!(Level::INFO, "listening on {}", listen_address);
    axum::serve(listener, app).await.unwrap();
}
