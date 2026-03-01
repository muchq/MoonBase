use axum::{extract::State, http::StatusCode, response::Json};
use std::sync::Arc;
use tracing::error;

use crate::{
    AppState,
    types::{ErrorResponse, PartitionInfo, PartitionsResponse},
};

pub async fn partitions_handler(
    State(state): State<Arc<AppState>>,
) -> Result<Json<PartitionsResponse>, (StatusCode, Json<ErrorResponse>)> {
    let partitions = scan_partitions(&state.data_dir).map_err(|e| {
        error!("{}", e);
        (
            StatusCode::INTERNAL_SERVER_ERROR,
            Json(ErrorResponse { error: e }),
        )
    })?;
    Ok(Json(PartitionsResponse { partitions }))
}

/// Scans the Hive-partitioned directory tree under `{data_dir}/game_features/`
/// and returns one `PartitionInfo` per `platform=X/month=Y` leaf that
/// contains a `data.parquet` file.
fn scan_partitions(data_dir: &str) -> Result<Vec<PartitionInfo>, String> {
    let mut partitions = Vec::new();
    let table_dir = std::path::Path::new(data_dir).join("game_features");
    if !table_dir.exists() {
        return Ok(partitions);
    }

    for platform_entry in std::fs::read_dir(&table_dir).map_err(|e| e.to_string())? {
        let platform_entry = platform_entry.map_err(|e| e.to_string())?;
        let dir_name = platform_entry.file_name();
        let dir_str = dir_name.to_string_lossy();
        let Some(platform) = dir_str.strip_prefix("platform=") else {
            continue;
        };

        for month_entry in
            std::fs::read_dir(platform_entry.path()).map_err(|e| e.to_string())?
        {
            let month_entry = month_entry.map_err(|e| e.to_string())?;
            let month_name = month_entry.file_name();
            let month_str = month_name.to_string_lossy();
            let Some(month) = month_str.strip_prefix("month=") else {
                continue;
            };

            if month_entry.path().join("data.parquet").exists() {
                partitions.push(PartitionInfo {
                    table: "game_features".to_string(),
                    platform: platform.to_string(),
                    month: month.to_string(),
                });
            }
        }
    }

    partitions.sort_by(|a, b| {
        a.platform
            .cmp(&b.platform)
            .then(a.month.cmp(&b.month))
    });
    Ok(partitions)
}
