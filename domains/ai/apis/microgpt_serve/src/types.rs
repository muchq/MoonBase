use serde::{Deserialize, Serialize};

// ---------- Generate ----------

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

    /// Maximum tokens to generate per sample. Omit for model default (block_size).
    pub max_tokens: Option<usize>,
}

impl GenerateRequest {
    pub fn validate(&self) -> Result<(), String> {
        if self.temperature < 0.0 {
            return Err(format!(
                "temperature must be >= 0, got {}",
                self.temperature
            ));
        }
        if self.num_samples == 0 {
            return Err("num_samples must be >= 1".to_string());
        }
        if let Some(0) = self.max_tokens {
            return Err("max_tokens must be >= 1 when specified".to_string());
        }
        Ok(())
    }
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

    /// Maximum tokens to generate. Omit for model default (block_size - prompt_len).
    pub max_tokens: Option<usize>,
}

impl ChatRequest {
    pub fn validate(&self) -> Result<(), String> {
        if self.messages.is_empty() {
            return Err("messages must not be empty".to_string());
        }
        if self.temperature < 0.0 {
            return Err(format!(
                "temperature must be >= 0, got {}",
                self.temperature
            ));
        }
        if let Some(0) = self.max_tokens {
            return Err("max_tokens must be >= 1 when specified".to_string());
        }
        for (i, msg) in self.messages.iter().enumerate() {
            if msg.role != "user" && msg.role != "assistant" {
                return Err(format!("messages[{}]: unknown role {:?}", i, msg.role));
            }
            if msg.content.trim().is_empty() {
                return Err(format!("messages[{}]: content must not be empty", i));
            }
        }
        Ok(())
    }
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

#[cfg(test)]
mod tests {
    use super::*;

    // ---- GenerateRequest defaults ----

    #[test]
    fn generate_request_defaults() {
        let req: GenerateRequest = serde_json::from_str("{}").unwrap();
        assert_eq!(req.num_samples, 1);
        assert_eq!(req.temperature, 0.5);
        assert_eq!(req.seed, 42);
        assert!(req.max_tokens.is_none());
    }

    #[test]
    fn generate_request_explicit_values() {
        let req: GenerateRequest = serde_json::from_str(
            r#"{"num_samples":3,"temperature":0.8,"seed":99,"max_tokens":10}"#,
        )
        .unwrap();
        assert_eq!(req.num_samples, 3);
        assert_eq!(req.temperature, 0.8);
        assert_eq!(req.seed, 99);
        assert_eq!(req.max_tokens, Some(10));
    }

    // ---- GenerateRequest validation ----

    #[test]
    fn generate_validate_ok_defaults() {
        let req: GenerateRequest = serde_json::from_str("{}").unwrap();
        assert!(req.validate().is_ok());
    }

    #[test]
    fn generate_validate_ok_temperature_zero() {
        let req: GenerateRequest =
            serde_json::from_str(r#"{"temperature":0.0}"#).unwrap();
        assert!(req.validate().is_ok());
    }

    #[test]
    fn generate_validate_rejects_negative_temperature() {
        let req: GenerateRequest =
            serde_json::from_str(r#"{"temperature":-0.1}"#).unwrap();
        let err = req.validate().unwrap_err();
        assert!(err.contains("temperature"), "{err}");
    }

    #[test]
    fn generate_validate_rejects_zero_num_samples() {
        let req: GenerateRequest =
            serde_json::from_str(r#"{"num_samples":0}"#).unwrap();
        let err = req.validate().unwrap_err();
        assert!(err.contains("num_samples"), "{err}");
    }

    #[test]
    fn generate_validate_rejects_zero_max_tokens() {
        let req: GenerateRequest =
            serde_json::from_str(r#"{"max_tokens":0}"#).unwrap();
        let err = req.validate().unwrap_err();
        assert!(err.contains("max_tokens"), "{err}");
    }

    #[test]
    fn generate_validate_ok_max_tokens_none() {
        let req: GenerateRequest = serde_json::from_str("{}").unwrap();
        assert!(req.validate().is_ok());
    }

    #[test]
    fn generate_validate_ok_max_tokens_some() {
        let req: GenerateRequest =
            serde_json::from_str(r#"{"max_tokens":5}"#).unwrap();
        assert!(req.validate().is_ok());
    }

    // ---- ChatRequest defaults ----

    #[test]
    fn chat_request_defaults() {
        let req: ChatRequest = serde_json::from_str(
            r#"{"messages":[{"role":"user","content":"hi"}]}"#,
        )
        .unwrap();
        assert_eq!(req.temperature, 0.5);
        assert_eq!(req.seed, 42);
        assert!(req.max_tokens.is_none());
        assert_eq!(req.messages.len(), 1);
    }

    #[test]
    fn chat_request_explicit_values() {
        let req: ChatRequest = serde_json::from_str(
            r#"{"messages":[{"role":"user","content":"hi"}],"temperature":0.9,"seed":7,"max_tokens":20}"#,
        )
        .unwrap();
        assert_eq!(req.temperature, 0.9);
        assert_eq!(req.seed, 7);
        assert_eq!(req.max_tokens, Some(20));
    }

    // ---- ChatRequest validation ----

    #[test]
    fn chat_validate_ok_single_message() {
        let req: ChatRequest = serde_json::from_str(
            r#"{"messages":[{"role":"user","content":"hello"}]}"#,
        )
        .unwrap();
        assert!(req.validate().is_ok());
    }

    #[test]
    fn chat_validate_ok_multi_turn() {
        let req: ChatRequest = serde_json::from_str(
            r#"{"messages":[{"role":"user","content":"hi"},{"role":"assistant","content":"hello"},{"role":"user","content":"bye"}]}"#,
        )
        .unwrap();
        assert!(req.validate().is_ok());
    }

    #[test]
    fn chat_validate_rejects_empty_messages() {
        let req: ChatRequest =
            serde_json::from_str(r#"{"messages":[]}"#).unwrap();
        let err = req.validate().unwrap_err();
        assert!(err.contains("empty"), "{err}");
    }

    #[test]
    fn chat_validate_rejects_negative_temperature() {
        let req: ChatRequest = serde_json::from_str(
            r#"{"messages":[{"role":"user","content":"hi"}],"temperature":-1}"#,
        )
        .unwrap();
        let err = req.validate().unwrap_err();
        assert!(err.contains("temperature"), "{err}");
    }

    #[test]
    fn chat_validate_rejects_zero_max_tokens() {
        let req: ChatRequest = serde_json::from_str(
            r#"{"messages":[{"role":"user","content":"hi"}],"max_tokens":0}"#,
        )
        .unwrap();
        let err = req.validate().unwrap_err();
        assert!(err.contains("max_tokens"), "{err}");
    }

    #[test]
    fn chat_validate_rejects_unknown_role() {
        let req: ChatRequest = serde_json::from_str(
            r#"{"messages":[{"role":"system","content":"you are helpful"}]}"#,
        )
        .unwrap();
        let err = req.validate().unwrap_err();
        assert!(err.contains("unknown role"), "{err}");
    }

    #[test]
    fn chat_validate_rejects_empty_content() {
        let req: ChatRequest = serde_json::from_str(
            r#"{"messages":[{"role":"user","content":""}]}"#,
        )
        .unwrap();
        let err = req.validate().unwrap_err();
        assert!(err.contains("content"), "{err}");
    }

    #[test]
    fn chat_validate_rejects_whitespace_only_content() {
        let req: ChatRequest = serde_json::from_str(
            r#"{"messages":[{"role":"user","content":"   "}]}"#,
        )
        .unwrap();
        let err = req.validate().unwrap_err();
        assert!(err.contains("content"), "{err}");
    }

    #[test]
    fn chat_validate_reports_index_of_bad_message() {
        let req: ChatRequest = serde_json::from_str(
            r#"{"messages":[{"role":"user","content":"hi"},{"role":"bot","content":"hey"}]}"#,
        )
        .unwrap();
        let err = req.validate().unwrap_err();
        assert!(err.contains("messages[1]"), "{err}");
    }

    // ---- Serialization ----

    #[test]
    fn error_response_serializes() {
        let resp = ErrorResponse {
            error: "something went wrong".to_string(),
        };
        let json = serde_json::to_string(&resp).unwrap();
        assert!(json.contains("something went wrong"));
    }

    #[test]
    fn chat_response_serializes_all_fields() {
        let resp = ChatResponse {
            role: "assistant".to_string(),
            content: "hello".to_string(),
            tokens_dropped: 5,
        };
        let json = serde_json::to_value(&resp).unwrap();
        assert_eq!(json["role"], "assistant");
        assert_eq!(json["content"], "hello");
        assert_eq!(json["tokens_dropped"], 5);
    }

    #[test]
    fn generate_response_serializes() {
        let resp = GenerateResponse {
            samples: vec!["a".to_string(), "b".to_string()],
        };
        let json = serde_json::to_value(&resp).unwrap();
        assert_eq!(json["samples"].as_array().unwrap().len(), 2);
    }
}
