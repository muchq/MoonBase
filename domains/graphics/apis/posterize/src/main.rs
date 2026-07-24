mod images;
mod service;
mod types;

use crate::service::{blur_post, edges_post};
use axum::routing::post;
use server_pal::{listen_addr_pal, router_builder, serve};
use tracing::{Level, event};

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt::init();
    // Keeps the exporter alive for the process lifetime; without this the
    // http_server_* instruments record into the no-op global meter.
    let _otel_provider = server_pal::init_otel();

    let listen_address = listen_addr_pal();

    let app = router_builder()
        .route("/imagine/v1/blur", post(blur_post))
        .route("/imagine/v1/edges", post(edges_post))
        .build();

    event!(Level::INFO, "listening on {}", listen_address);
    serve(app, &listen_address).await;
}
