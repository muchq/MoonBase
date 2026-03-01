use arrow::json::ReaderBuilder;
use axum::{extract::State, http::StatusCode, response::Json};
use parquet::arrow::ArrowWriter;
use std::fs;
use std::io::Cursor;
use std::sync::Arc;
use tracing::error;

use crate::{
    AppState,
    catalog::{game_features_schema, game_pgns_schema, motif_occurrences_schema},
    types::{ErrorResponse, WriteRequest, WriteResponse},
};

pub async fn write_handler(
    State(state): State<Arc<AppState>>,
    Json(req): Json<WriteRequest>,
) -> Result<Json<WriteResponse>, (StatusCode, Json<ErrorResponse>)> {
    let schema = match req.table.as_str() {
        "game_features" => game_features_schema(),
        "motif_occurrences" => motif_occurrences_schema(),
        "game_pgns" => game_pgns_schema(),
        other => return Err(bad_request(&format!("Unknown table: {other}"))),
    };

    // Serialize rows to newline-delimited JSON for the Arrow JSON reader
    let ndjson: Vec<u8> = req
        .rows
        .iter()
        .flat_map(|v| {
            let mut s = v.to_string();
            s.push('\n');
            s.into_bytes()
        })
        .collect();

    let cursor = Cursor::new(ndjson);
    let reader = ReaderBuilder::new(Arc::clone(&schema))
        .build(cursor)
        .map_err(|e| app_error(&e.to_string()))?;

    let partition_dir = format!(
        "{}/{}/platform={}/month={}",
        state.data_dir, req.table, req.platform, req.month
    );
    fs::create_dir_all(&partition_dir).map_err(|e| app_error(&e.to_string()))?;
    let parquet_path = format!("{partition_dir}/data.parquet");

    let file = fs::File::create(&parquet_path).map_err(|e| app_error(&e.to_string()))?;
    let mut writer = ArrowWriter::try_new(file, Arc::clone(&schema), None)
        .map_err(|e| app_error(&e.to_string()))?;

    let mut rows_written = 0;
    for batch_result in reader {
        let batch = batch_result.map_err(|e| app_error(&e.to_string()))?;
        rows_written += batch.num_rows();
        writer
            .write(&batch)
            .map_err(|e| app_error(&e.to_string()))?;
    }
    writer.close().map_err(|e| app_error(&e.to_string()))?;

    Ok(Json(WriteResponse { rows_written }))
}

fn app_error(msg: &str) -> (StatusCode, Json<ErrorResponse>) {
    error!("{}", msg);
    (
        StatusCode::INTERNAL_SERVER_ERROR,
        Json(ErrorResponse {
            error: msg.to_string(),
        }),
    )
}

fn bad_request(msg: &str) -> (StatusCode, Json<ErrorResponse>) {
    (
        StatusCode::BAD_REQUEST,
        Json(ErrorResponse {
            error: msg.to_string(),
        }),
    )
}
