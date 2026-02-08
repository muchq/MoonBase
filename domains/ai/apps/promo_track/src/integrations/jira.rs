use std::future::Future;
use std::pin::Pin;

use crate::evidence::{EvidenceCard, EvidenceSource};

use super::{Connector, ConnectorError};

/// Jira integration — reads epic ownership, cross-team tickets, and
/// project-level scope indicators.
pub struct JiraConnector {
    token: Option<String>,
    base_url: Option<String>,
}

impl JiraConnector {
    pub fn new() -> Self {
        Self {
            token: std::env::var("STAFFTRACK_JIRA_TOKEN").ok(),
            base_url: std::env::var("STAFFTRACK_JIRA_URL").ok(),
        }
    }
}

impl Connector for JiraConnector {
    fn name(&self) -> &str {
        "Jira"
    }

    fn is_configured(&self) -> bool {
        self.token.is_some() && self.base_url.is_some()
    }

    fn pull(&self) -> Pin<Box<dyn Future<Output = Result<Vec<EvidenceCard>, ConnectorError>> + Send + '_>> {
        Box::pin(async move {
            if !self.is_configured() {
                return Err(ConnectorError::NotConfigured(
                    "Set STAFFTRACK_JIRA_TOKEN and STAFFTRACK_JIRA_URL to enable Jira integration"
                        .into(),
                ));
            }

            // POC stub — real implementation will fetch:
            // - Epics owned / driven
            // - Cross-team ticket involvement
            // - Planning and roadmap artifacts
            tracing::info!("Jira connector: pull not yet implemented (POC stub)");
            Ok(vec![EvidenceCard::new(
                EvidenceSource::Jira,
                "[stub] example Jira evidence card",
            )])
        })
    }
}
