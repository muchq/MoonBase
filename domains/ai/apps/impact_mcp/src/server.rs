use std::sync::{Arc, Mutex};

use chrono::Utc;
use rmcp::handler::server::tool::ToolRouter;
use rmcp::handler::server::wrapper::Parameters;
use rmcp::model::{Implementation, ServerCapabilities, ServerInfo};
use rmcp::{tool, tool_handler, tool_router, ServerHandler};
use schemars::JsonSchema;
use serde::Deserialize;

use crate::archetype::{Archetype, ArchetypeProfile};
use crate::evidence::{EvidenceCard, EvidenceSource, EvidenceStore};
use crate::readiness::{CoverageLevel, ReadinessMap};
use crate::rubric::Rubric;

/// MCP server exposing impact-mcp tools over stdio.
#[derive(Clone)]
pub struct ImpactServer {
    store: Arc<Mutex<EvidenceStore>>,
    rubric: Arc<Rubric>,
    tool_router: ToolRouter<Self>,
}

#[derive(Deserialize, JsonSchema)]
pub struct AddEvidenceParams {
    /// Summary of the evidence (1-3 sentences).
    pub summary: String,
    /// Source: github, jira, slack, gdocs, or manual.
    pub source: Option<String>,
    /// URL or reference link.
    pub link: Option<String>,
    /// Comma-separated rubric dimension tags (e.g. "scope,leverage").
    pub rubric_tags: Option<String>,
    /// Comma-separated archetype tags (e.g. "tech_lead,architect").
    pub archetype_tags: Option<String>,
}

impl ImpactServer {
    pub fn new(store: EvidenceStore, rubric: Rubric) -> Self {
        Self {
            store: Arc::new(Mutex::new(store)),
            rubric: Arc::new(rubric),
            tool_router: Self::tool_router(),
        }
    }
}

#[tool_router]
impl ImpactServer {
    /// List all evidence cards in the store.
    #[tool(name = "list_evidence", description = "List all evidence cards")]
    async fn list_evidence(&self) -> String {
        let mut store = self.store.lock().unwrap();
        let _ = store.refresh();
        let cards = store.all();
        if cards.is_empty() {
            return "No evidence cards yet.".to_string();
        }
        let mut out = format!("{} evidence card(s):\n\n", cards.len());
        for card in &cards {
            let tags = if card.rubric_tags.is_empty() {
                String::new()
            } else {
                format!(" [{}]", card.rubric_tags.join(", "))
            };
            out.push_str(&format!(
                "  [{source}] {date} — {summary} (confidence: {conf:.0}%){tags}\n",
                source = card.source,
                date = card.timestamp.format("%Y-%m-%d"),
                summary = card.summary,
                conf = card.confidence * 100.0,
            ));
        }
        out
    }

    /// Add a new evidence card.
    #[tool(name = "add_evidence", description = "Add a new evidence card")]
    async fn add_evidence(&self, params: Parameters<AddEvidenceParams>) -> String {
        let p = params.0;
        let src = match p.source.as_deref().unwrap_or("manual").to_lowercase().as_str() {
            "github" => EvidenceSource::Github,
            "jira" => EvidenceSource::Jira,
            "slack" => EvidenceSource::Slack,
            "gdocs" => EvidenceSource::Gdocs,
            _ => EvidenceSource::Manual,
        };

        let mut card = EvidenceCard::new(src, &p.summary);
        if let Some(l) = p.link {
            card = card.with_link(l);
        }
        if let Some(tags) = p.rubric_tags {
            card = card.with_rubric_tags(
                tags.split(',').map(|s| s.trim().to_string()).collect(),
            );
        }
        if let Some(tags) = p.archetype_tags {
            let archetypes: Vec<Archetype> = tags
                .split(',')
                .filter_map(|s| match s.trim().to_lowercase().as_str() {
                    "tech_lead" | "techlead" => Some(Archetype::TechLead),
                    "architect" => Some(Archetype::Architect),
                    "problem_solver" | "problemsolver" => Some(Archetype::ProblemSolver),
                    "operator" => Some(Archetype::Operator),
                    "mentor" => Some(Archetype::Mentor),
                    _ => None,
                })
                .collect();
            card = card.with_archetype_tags(archetypes);
        }

        let mut store = self.store.lock().unwrap();
        match store.insert(card) {
            Ok(()) => "Evidence card added.".to_string(),
            Err(e) => format!("Error adding evidence: {e}"),
        }
    }

    /// Compute the overall readiness score and per-dimension breakdown.
    #[tool(name = "get_readiness", description = "Compute readiness score")]
    async fn get_readiness(&self) -> String {
        let mut store = self.store.lock().unwrap();
        let _ = store.refresh();
        let readiness = ReadinessMap::compute(&store, &self.rubric);
        let mut out = format!(
            "Overall readiness: {:.0}%\n\nDimension breakdown:\n",
            readiness.overall_score * 100.0,
        );
        for dim in &self.rubric.dimensions {
            let level = readiness
                .dimension_coverage
                .get(&dim.key)
                .copied()
                .unwrap_or(CoverageLevel::None);
            out.push_str(&format!("  {:<12} {}\n", dim.label, level));
        }
        out
    }

    /// Show gaps in readiness and suggestions to close them.
    #[tool(name = "get_gaps", description = "Show gaps in readiness")]
    async fn get_gaps(&self) -> String {
        let mut store = self.store.lock().unwrap();
        let _ = store.refresh();
        let readiness = ReadinessMap::compute(&store, &self.rubric);
        if readiness.gaps.is_empty() {
            return "No gaps — all dimensions have strong coverage!".to_string();
        }
        let mut out = format!("{} gap(s) identified:\n\n", readiness.gaps.len());
        for gap in &readiness.gaps {
            out.push_str(&format!(
                "  {} ({}): {}\n",
                gap.dimension, gap.coverage, gap.suggestion,
            ));
        }
        out
    }

    /// List staff archetypes with current evidence strengths.
    #[tool(
        name = "list_archetypes",
        description = "List staff archetypes with current strengths"
    )]
    async fn list_archetypes(&self) -> String {
        let mut store = self.store.lock().unwrap();
        let _ = store.refresh();
        let total = store.count().max(1) as f64;
        let mut out = String::from("Staff Archetypes:\n\n");
        for arch in Archetype::ALL {
            let count = store.by_archetype(arch).len() as f64;
            let strength = count / total;
            out.push_str(&format!(
                "  {:<15} {:<50} ({:.0}% evidence)\n",
                arch.label(),
                arch.description(),
                strength * 100.0,
            ));
        }
        out
    }

    /// Show the active rubric name, version, and dimensions.
    #[tool(name = "show_rubric", description = "Show the active rubric")]
    async fn show_rubric(&self) -> String {
        let mut out = format!(
            "Active rubric: {} (v{})\n\n",
            self.rubric.name, self.rubric.version,
        );
        for dim in &self.rubric.dimensions {
            out.push_str(&format!(
                "  {:<12} (weight {:.1}): {}\n",
                dim.label, dim.weight, dim.description,
            ));
        }
        out
    }

    /// Draft a weekly status update from recent evidence.
    #[tool(name = "draft_status", description = "Draft a weekly status update")]
    async fn draft_status(&self) -> String {
        let mut store = self.store.lock().unwrap();
        let _ = store.refresh();
        let now = Utc::now();
        let mut out = format!(
            "── Weekly Status Draft ({}) ──\n\n",
            now.format("%Y-%m-%d"),
        );

        let cards = store.all();
        if cards.is_empty() {
            out.push_str(
                "No evidence cards yet. Add evidence from your integrations \
                 or manually to generate a status update.\n",
            );
            return out;
        }

        let mut recent: Vec<_> = cards;
        recent.sort_by(|a, b| b.timestamp.cmp(&a.timestamp));
        let week_items: Vec<_> = recent.into_iter().take(7).collect();

        out.push_str("Key accomplishments this week:\n\n");
        for card in &week_items {
            let tags = if card.rubric_tags.is_empty() {
                String::new()
            } else {
                format!(" [{}]", card.rubric_tags.join(", "))
            };
            out.push_str(&format!("  • {}{}\n", card.summary, tags));
        }

        // Infer dominant archetype from evidence
        let profile = infer_archetype_profile(&store);
        if let Some(dominant) = profile.dominant() {
            out.push_str(&format!(
                "\nNarrative angle ({}): Frame updates around {} to reinforce \
                 your Staff shape.\n",
                dominant.label(),
                dominant.description().to_lowercase(),
            ));
        }

        out.push_str("\n── END DRAFT ──\n");
        out
    }

    /// Generate a promotion packet section from current evidence and rubric.
    #[tool(
        name = "draft_packet",
        description = "Generate a promotion packet section"
    )]
    async fn draft_packet(&self) -> String {
        let mut store = self.store.lock().unwrap();
        let _ = store.refresh();
        let readiness = ReadinessMap::compute(&store, &self.rubric);
        let mut out = String::from("═══ PROMOTION PACKET DRAFT ═══\n\n");

        out.push_str(&format!(
            "Readiness: {:.0}% against \"{}\"\n\n",
            readiness.overall_score * 100.0,
            self.rubric.name,
        ));

        let profile = infer_archetype_profile(&store);
        if let Some(dominant) = profile.dominant() {
            out.push_str(&format!(
                "Primary archetype: {} — {}\n\n",
                dominant.label(),
                dominant.description(),
            ));
        }

        out.push_str("── Evidence by Dimension ──\n\n");
        for dim in &self.rubric.dimensions {
            let cards = store.by_rubric_tag(&dim.key);
            let level = readiness
                .dimension_coverage
                .get(&dim.key)
                .copied()
                .unwrap_or(CoverageLevel::None);

            out.push_str(&format!("{}  [{}]\n", dim.label, level));
            if cards.is_empty() {
                out.push_str("  (no evidence yet)\n");
            } else {
                for card in cards.iter().take(5) {
                    out.push_str(&format!("  • {} ({})\n", card.summary, card.source));
                }
                let remaining = cards.len().saturating_sub(5);
                if remaining > 0 {
                    out.push_str(&format!("  ... and {remaining} more\n"));
                }
            }
            out.push('\n');
        }

        out.push_str("── Archetype Strengths ──\n\n");
        for arch in Archetype::ALL {
            let strength = readiness
                .archetype_strength
                .get(arch.label())
                .copied()
                .unwrap_or(0.0);
            out.push_str(&format!(
                "  {:<15} {:.0}%\n",
                arch.label(),
                strength * 100.0,
            ));
        }

        if !readiness.gaps.is_empty() {
            out.push_str("\n── Gaps to Address ──\n\n");
            for gap in &readiness.gaps {
                out.push_str(&format!(
                    "  • {} ({}): {}\n",
                    gap.dimension, gap.coverage, gap.suggestion,
                ));
            }
        }

        out.push_str("\n═══ END DRAFT ═══\n");
        out
    }
}

#[tool_handler]
impl ServerHandler for ImpactServer {
    fn get_info(&self) -> ServerInfo {
        ServerInfo {
            server_info: Implementation {
                name: "impact-mcp".into(),
                version: env!("CARGO_PKG_VERSION").into(),
                title: None,
                icons: None,
                website_url: None,
            },
            capabilities: ServerCapabilities {
                tools: Some(Default::default()),
                ..Default::default()
            },
            ..Default::default()
        }
    }
}

/// Infer an archetype profile from evidence store data.
fn infer_archetype_profile(store: &EvidenceStore) -> ArchetypeProfile {
    let total = store.count().max(1) as f64;
    let inferred: Vec<(Archetype, f64)> = Archetype::ALL
        .iter()
        .map(|&arch| {
            let count = store.by_archetype(arch).len() as f64;
            (arch, count / total)
        })
        .collect();
    ArchetypeProfile {
        selected: Vec::new(),
        inferred,
    }
}
