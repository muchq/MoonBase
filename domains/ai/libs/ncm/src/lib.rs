pub mod agent;
pub mod data;
pub mod model;
pub mod pipeline;
pub type Result<T> = std::result::Result<T, Box<dyn std::error::Error + Send + Sync>>;
