use crate::evidence::{EvidenceCard, EvidenceSource};

use super::{Connector, ConnectorError};

/// GitHub integration — reads PRs, reviews, RFC discussions, and commits.
pub struct GithubConnector {
    token: Option<String>,
}

impl GithubConnector {
    pub fn new() -> Self {
        Self {
            token: std::env::var("STAFFTRACK_GITHUB_TOKEN").ok(),
        }
    }
}

impl Connector for GithubConnector {
    fn name(&self) -> &str {
        "GitHub"
    }

    fn is_configured(&self) -> bool {
        self.token.is_some()
    }

    fn pull(&self) -> Result<Vec<EvidenceCard>, ConnectorError> {
        if !self.is_configured() {
            return Err(ConnectorError::NotConfigured(
                "Set STAFFTRACK_GITHUB_TOKEN to enable GitHub integration".into(),
            ));
        }

        // POC stub — real implementation will use the GitHub API to fetch:
        // - PRs authored & reviewed
        // - RFC/ADR discussions
        // - Cross-repo contributions
        // - Release participation
        tracing::info!("GitHub connector: pull not yet implemented (POC stub)");
        Ok(vec![EvidenceCard::new(
            EvidenceSource::Github,
            "[stub] example GitHub evidence card",
        )])
    }
}
