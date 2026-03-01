use arrow::json::ArrayWriter;
use axum::{extract::State, http::StatusCode, response::Json};
use std::sync::Arc;
use tracing::error;

use crate::{
    AppState,
    types::{ErrorResponse, QueryRequest, QueryResponse},
};

pub async fn query_handler(
    State(state): State<Arc<AppState>>,
    Json(req): Json<QueryRequest>,
) -> Result<Json<QueryResponse>, (StatusCode, Json<ErrorResponse>)> {
    let df = state
        .ctx
        .sql(&req.sql)
        .await
        .map_err(|e| app_error(&e.to_string()))?;

    let df = if req.limit.is_some() || req.offset.is_some() {
        df.limit(req.offset.unwrap_or(0), req.limit)
            .map_err(|e| app_error(&e.to_string()))?
    } else {
        df
    };

    let batches = df
        .collect()
        .await
        .map_err(|e| app_error(&e.to_string()))?;

    let mut buf = Vec::new();
    {
        let mut writer = ArrayWriter::new(&mut buf);
        let batch_refs: Vec<&datafusion::arrow::record_batch::RecordBatch> =
            batches.iter().collect();
        writer
            .write_batches(&batch_refs)
            .map_err(|e| app_error(&e.to_string()))?;
        writer.finish().map_err(|e| app_error(&e.to_string()))?;
    }

    let rows: Vec<serde_json::Value> =
        serde_json::from_slice(&buf).map_err(|e| app_error(&e.to_string()))?;
    let row_count = rows.len();

    Ok(Json(QueryResponse { rows, row_count }))
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
