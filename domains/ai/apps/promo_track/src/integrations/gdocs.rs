use crate::evidence::{EvidenceCard, EvidenceSource};

use super::{Connector, ConnectorError};

/// Google Docs integration — reads design docs, RFCs, post-mortems,
/// and other long-form technical writing.
pub struct GdocsConnector {
    credentials: Option<String>,
}

impl GdocsConnector {
    pub fn new() -> Self {
        Self {
            credentials: std::env::var("STAFFTRACK_GDOCS_CREDENTIALS").ok(),
        }
    }
}

impl Connector for GdocsConnector {
    fn name(&self) -> &str {
        "Google Docs"
    }

    fn is_configured(&self) -> bool {
        self.credentials.is_some()
    }

    fn pull(&self) -> Result<Vec<EvidenceCard>, ConnectorError> {
        if !self.is_configured() {
            return Err(ConnectorError::NotConfigured(
                "Set STAFFTRACK_GDOCS_CREDENTIALS to enable Google Docs integration".into(),
            ));
        }

        // POC stub — real implementation will fetch:
        // - Design docs authored
        // - RFC contributions
        // - Post-mortem / incident docs
        // - Strategy documents
        tracing::info!("Google Docs connector: pull not yet implemented (POC stub)");
        Ok(vec![EvidenceCard::new(
            EvidenceSource::Gdocs,
            "[stub] example Google Docs evidence card",
        )])
    }
}
