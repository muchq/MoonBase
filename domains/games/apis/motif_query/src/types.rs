use serde::{Deserialize, Serialize};

#[derive(Deserialize)]
pub struct QueryRequest {
    pub sql: String,
    pub limit: Option<usize>,
    pub offset: Option<usize>,
}

#[derive(Serialize)]
pub struct QueryResponse {
    pub rows: Vec<serde_json::Value>,
    pub row_count: usize,
}

#[derive(Deserialize)]
pub struct WriteRequest {
    pub platform: String,
    pub month: String,
    pub table: String,
    pub rows: Vec<serde_json::Value>,
}

#[derive(Serialize)]
pub struct WriteResponse {
    pub rows_written: usize,
}

#[derive(Serialize)]
pub struct PartitionInfo {
    pub table: String,
    pub platform: String,
    pub month: String,
}

#[derive(Serialize)]
pub struct PartitionsResponse {
    pub partitions: Vec<PartitionInfo>,
}

#[derive(Serialize)]
pub struct ErrorResponse {
    pub error: String,
}
