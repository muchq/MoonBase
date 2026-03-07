use std::future::Future;
use std::pin::Pin;

use crate::evidence::{EvidenceCard, EvidenceSource};

use super::{Connector, ConnectorError};

/// Slack integration — reads threads where the user demonstrated influence,
/// alignment, or mentorship.
pub struct SlackConnector {
    token: Option<String>,
}

impl SlackConnector {
    pub fn new() -> Self {
        Self {
            token: std::env::var("IMPACT_MCP_SLACK_TOKEN").ok(),
        }
    }
}

impl Connector for SlackConnector {
    fn name(&self) -> &str {
        "Slack"
    }

    fn is_configured(&self) -> bool {
        self.token.is_some()
    }

    fn pull(&self) -> Pin<Box<dyn Future<Output = Result<Vec<EvidenceCard>, ConnectorError>> + Send + '_>> {
        Box::pin(async move {
            if !self.is_configured() {
                return Err(ConnectorError::NotConfigured(
                    "Set IMPACT_MCP_SLACK_TOKEN to enable Slack integration".into(),
                ));
            }

            // POC stub — real implementation will fetch:
            // - Threads showing cross-team alignment
            // - Mentorship / teaching moments
            // - Technical decision discussions
            tracing::info!("Slack connector: pull not yet implemented (POC stub)");
            Ok(vec![EvidenceCard::new(
                EvidenceSource::Slack,
                "[stub] example Slack evidence card",
            )])
        })
    }
}
