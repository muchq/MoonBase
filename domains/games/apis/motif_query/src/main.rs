mod catalog;
mod partitions;
mod query;
mod types;
mod writer;

use axum::routing::{get, post};
use catalog::build_context;
use datafusion::prelude::SessionContext;
use server_pal::{listen_addr_pal, router_builder, serve};
use std::sync::Arc;
use tracing::{Level, event};

#[derive(Clone)]
pub struct AppState {
    pub ctx: SessionContext,
    pub data_dir: String,
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt::init();

    let data_dir =
        std::env::var("PARQUET_DATA_DIR").unwrap_or_else(|_| "/data/parquet".to_string());

    let ctx = build_context(&data_dir)
        .await
        .expect("Failed to initialize DataFusion catalog");

    let state = Arc::new(AppState { ctx, data_dir });
    let listen_address = listen_addr_pal();

    let app = router_builder()
        .route("/v1/query", post(query::query_handler))
        .route("/v1/write", post(writer::write_handler))
        .route("/v1/partitions", get(partitions::partitions_handler))
        .build()
        .with_state(state);

    event!(Level::INFO, "motif_query listening on {listen_address}");
    serve(app, &listen_address).await;
}
