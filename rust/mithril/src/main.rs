use axum::{
    routing::get,
    routing::post,
    response::Json,
    Router,
};
use serde_json::{Value, json};
use std::env;
use tracing::{event, Level};
use tracing_subscriber;

const DEFAULT_PORT: u16 = 8080;

async fn wordchain_post() -> Json<Value> {
    Json(json!({"data": "Hello"}))
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt::init();

    let port = env::var("PORT")
        .ok()
        .and_then(|p| p.parse::<u16>().ok())
        .unwrap_or(DEFAULT_PORT);

    let listen_address = format!("0.0.0.0:{}", &port);

    // build our application with a single route
    let app = Router::new()
        .route("/health", get(|| async { "Ok" }))
        .route("/v1/wordchain", post(wordchain_post));

    let listener = tokio::net::TcpListener::bind(listen_address.clone()).await.unwrap();
    event!(Level::INFO, "listening on {}", listen_address);
    axum::serve(listener, app).await.unwrap();
}