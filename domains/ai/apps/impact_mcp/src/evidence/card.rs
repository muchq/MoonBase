use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::archetype::Archetype;

/// Where a piece of evidence originated.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum EvidenceSource {
    Github,
    Jira,
    Slack,
    Gdocs,
    Manual,
}

impl std::fmt::Display for EvidenceSource {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Github => write!(f, "GitHub"),
            Self::Jira => write!(f, "Jira"),
            Self::Slack => write!(f, "Slack"),
            Self::Gdocs => write!(f, "Google Docs"),
            Self::Manual => write!(f, "Manual"),
        }
    }
}

/// A single piece of evidence representing Staff-level impact.
///
/// Evidence cards are the atomic unit of the evidence store. They are
/// created by integration connectors or manually by the user, tagged
/// against the active rubric and relevant archetypes.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EvidenceCard {
    pub id: Uuid,
    pub source: EvidenceSource,
    pub link: Option<String>,
    pub timestamp: DateTime<Utc>,
    pub summary: String,
    pub rubric_tags: Vec<String>,
    pub archetype_tags: Vec<Archetype>,
    pub excerpts: Vec<String>,
    pub confidence: f64,
    pub created_at: DateTime<Utc>,
}

impl EvidenceCard {
    pub fn new(source: EvidenceSource, summary: impl Into<String>) -> Self {
        let now = Utc::now();
        Self {
            id: Uuid::new_v4(),
            source,
            link: None,
            timestamp: now,
            summary: summary.into(),
            rubric_tags: Vec::new(),
            archetype_tags: Vec::new(),
            excerpts: Vec::new(),
            confidence: 1.0,
            created_at: now,
        }
    }

    pub fn with_link(mut self, link: impl Into<String>) -> Self {
        self.link = Some(link.into());
        self
    }

    pub fn with_rubric_tags(mut self, tags: Vec<String>) -> Self {
        self.rubric_tags = tags;
        self
    }

    pub fn with_archetype_tags(mut self, tags: Vec<Archetype>) -> Self {
        self.archetype_tags = tags;
        self
    }

    pub fn with_excerpts(mut self, excerpts: Vec<String>) -> Self {
        self.excerpts = excerpts;
        self
    }

    pub fn with_confidence(mut self, confidence: f64) -> Self {
        self.confidence = confidence.clamp(0.0, 1.0);
        self
    }

    pub fn with_timestamp(mut self, ts: DateTime<Utc>) -> Self {
        self.timestamp = ts;
        self
    }
}
