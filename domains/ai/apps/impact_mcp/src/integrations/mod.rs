pub mod github;
pub mod jira;
pub mod slack;

use std::future::Future;
use std::pin::Pin;

use crate::evidence::EvidenceCard;

/// Trait implemented by all integration connectors.
///
/// Connectors are **read-only** — they pull artifacts from external
/// systems and convert them into evidence cards. They never write
/// back or send messages on behalf of the user.
pub trait Connector: Send + Sync {
    /// Human-readable name of the integration.
    fn name(&self) -> &str;

    /// Pull recent artifacts and convert them to evidence cards.
    /// Returns a tuple of (evidence cards, warnings/prompts).
    /// Returns an empty vec when no new evidence is found.
    fn pull(&self) -> Pin<Box<dyn Future<Output = Result<(Vec<EvidenceCard>, Vec<String>), ConnectorError>> + Send + '_>>;

    /// Whether this connector has been configured with credentials.
    fn is_configured(&self) -> bool;
}

#[derive(Debug)]
pub enum ConnectorError {
    NotConfigured(String),
    Network(String),
    Parse(String),
    Mcp(String),
}

impl std::fmt::Display for ConnectorError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::NotConfigured(msg) => write!(f, "not configured: {msg}"),
            Self::Network(msg) => write!(f, "network error: {msg}"),
            Self::Parse(msg) => write!(f, "parse error: {msg}"),
            Self::Mcp(msg) => write!(f, "MCP error: {msg}"),
        }
    }
}

impl std::error::Error for ConnectorError {}
