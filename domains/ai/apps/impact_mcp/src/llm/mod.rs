mod context;

pub use context::build_system_prompt;

use genai::chat::{ChatMessage, ChatRequest};
use genai::Client;

/// Default model used when `IMPACT_MCP_MODEL` is not set.
/// The genai crate routes model names to the correct provider automatically
/// (e.g. "claude-*" -> Anthropic, "gpt-*" -> OpenAI, "gemini-*" -> Gemini).
const DEFAULT_MODEL: &str = "claude-sonnet-4-5-20250929";

/// Thin wrapper around genai providing impact-mcp-specific defaults.
pub struct LlmClient {
    client: Client,
    model: String,
    system_prompt: String,
}

impl LlmClient {
    /// Create a new client.
    ///
    /// The model is resolved from `IMPACT_MCP_MODEL` env var, falling back
    /// to Claude Sonnet. The provider API key is resolved by genai from
    /// standard env vars (`ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, etc.).
    pub fn new(system_prompt: String) -> Self {
        let model =
            std::env::var("IMPACT_MCP_MODEL").unwrap_or_else(|_| DEFAULT_MODEL.to_string());
        Self {
            client: Client::default(),
            model,
            system_prompt,
        }
    }

    /// Send a user message (with conversation history) and get a response.
    pub async fn chat(
        &self,
        history: &[(String, String)],
        user_msg: &str,
    ) -> Result<String, LlmError> {
        let mut messages = vec![ChatMessage::system(&self.system_prompt)];

        for (user, assistant) in history {
            messages.push(ChatMessage::user(user));
            messages.push(ChatMessage::assistant(assistant));
        }
        messages.push(ChatMessage::user(user_msg));

        let req = ChatRequest::new(messages);
        let response = self
            .client
            .exec_chat(&self.model, req, None)
            .await
            .map_err(|e| LlmError::Provider(e.to_string()))?;

        response
            .content_text_into_string()
            .ok_or(LlmError::EmptyResponse)
    }

    pub fn model(&self) -> &str {
        &self.model
    }
}

#[derive(Debug)]
pub enum LlmError {
    Provider(String),
    EmptyResponse,
}

impl std::fmt::Display for LlmError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Provider(msg) => write!(f, "LLM provider error: {msg}"),
            Self::EmptyResponse => write!(f, "LLM returned an empty response"),
        }
    }
}

impl std::error::Error for LlmError {}
