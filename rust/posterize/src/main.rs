mod images;
mod service;
mod types;

use crate::service::{blur_post, edges_post};
use axum::routing::post;
use server_pal::{listen_addr_pal, router_builder};
use tracing::{Level, event};

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt::init();

    let listen_address = listen_addr_pal();

    let app = router_builder()
        .route("/v1/imagine/blur", post(blur_post))
        .route("/v1/imagine/edges", post(edges_post))
        .build();

    let listener = tokio::net::TcpListener::bind(listen_address.clone())
        .await
        .unwrap();
    event!(Level::INFO, "listening on {}", listen_address);
    axum::serve(listener, app).await.unwrap();
}
