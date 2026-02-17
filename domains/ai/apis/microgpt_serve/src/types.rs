use serde::{Deserialize, Serialize};

// ---------- Generate (existing) ----------

#[derive(Deserialize)]
pub struct GenerateRequest {
    /// Number of samples to generate (default 1, max 50).
    #[serde(default = "default_num_samples")]
    pub num_samples: usize,

    /// Sampling temperature (default 0.5). Higher = more random.
    #[serde(default = "default_temperature")]
    pub temperature: f64,

    /// RNG seed for reproducibility (default 42).
    #[serde(default = "default_seed")]
    pub seed: u64,
}

fn default_num_samples() -> usize {
    1
}

fn default_temperature() -> f64 {
    0.5
}

fn default_seed() -> u64 {
    42
}

#[derive(Serialize)]
pub struct GenerateResponse {
    pub samples: Vec<String>,
}

// ---------- Chat ----------

#[derive(Deserialize)]
pub struct ChatRequest {
    /// Conversation messages. Each must have a "role" and "content".
    pub messages: Vec<Message>,

    /// Sampling temperature (default 0.5).
    #[serde(default = "default_temperature")]
    pub temperature: f64,

    /// RNG seed for reproducibility (default 42).
    #[serde(default = "default_seed")]
    pub seed: u64,
}

#[derive(Deserialize, Serialize, Clone)]
pub struct Message {
    /// "user" or "assistant".
    pub role: String,
    /// The text content of the message.
    pub content: String,
}

#[derive(Serialize)]
pub struct ChatResponse {
    pub role: String,
    pub content: String,
    /// Number of tokens dropped from early conversation history to fit within
    /// the context window. 0 means no truncation occurred.
    pub tokens_dropped: usize,
}

#[derive(Serialize)]
pub struct ErrorResponse {
    pub error: String,
}
