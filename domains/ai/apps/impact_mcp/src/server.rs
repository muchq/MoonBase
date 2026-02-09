use std::path::PathBuf;

use rmcp::handler::server::tool::ToolRouter;
use rmcp::handler::server::wrapper::Parameters;
use rmcp::model::{
    GetPromptRequestParam, GetPromptResult, Implementation, ListPromptsResult,
    PaginatedRequestParam, Prompt, PromptMessage, PromptMessageRole, ServerCapabilities,
    ServerInfo,
};
use rmcp::service::RequestContext;
use rmcp::{tool, tool_handler, tool_router, ErrorData, RoleServer, ServerHandler};
use schemars::JsonSchema;
use serde::Deserialize;
use uuid::Uuid;

use crate::archetype::{Archetype, ArchetypeProfile};
use crate::evidence::{EvidenceCard, EvidenceSource, EvidenceStore};
use crate::readiness::{CoverageLevel, ReadinessMap};
use crate::rubric::Rubric;

/// MCP server exposing impact-mcp tools over stdio.
#[derive(Clone)]
pub struct ImpactServer {
    data_dir: PathBuf,
    tool_router: ToolRouter<Self>,
}

#[derive(Deserialize, JsonSchema)]
pub struct InsertCardRequest {
    /// Summary of the evidence (1-3 sentences).
    pub summary: String,
    /// Source: github, jira, slack, gdocs, or manual.
    #[serde(default = "default_source")]
    pub source: String,
    /// Comma-separated rubric dimension tags (e.g. "scope,leverage").
    pub rubric_tags: Option<String>,
    /// Comma-separated archetype tags (e.g. "tech_lead,architect").
    pub archetype_tags: Option<String>,
    /// URL or reference link.
    pub link: Option<String>,
    /// Confidence level (0.0-1.0).
    #[serde(default = "default_confidence")]
    pub confidence: f64,
}

fn default_source() -> String {
    "manual".to_string()
}

fn default_confidence() -> f64 {
    1.0
}

#[derive(Deserialize, JsonSchema)]
pub struct ListCardsRequest {
    /// Filter by source (github, jira, slack, gdocs, manual).
    pub source: Option<String>,
    /// Filter by rubric dimension tag.
    pub rubric_tag: Option<String>,
    /// Filter by archetype tag.
    pub archetype_tag: Option<String>,
    /// Maximum number of cards to return.
    pub limit: Option<usize>,
}

#[derive(Deserialize, JsonSchema)]
pub struct DeleteCardRequest {
    /// UUID of the card to delete.
    pub id: String,
}

#[derive(Deserialize, JsonSchema)]
pub struct SetRubricRequest {
    /// YAML content of the new rubric.
    pub yaml: String,
}

#[derive(Deserialize, JsonSchema)]
pub struct PullSourceRequest {
    /// Source to pull from: github, jira, slack, or gdocs.
    pub source: String,
}

impl ImpactServer {
    pub fn new(data_dir: PathBuf) -> Self {
        Self {
            data_dir,
            tool_router: Self::tool_router(),
        }
    }

    fn load_rubric(&self) -> Rubric {
        let rubric_path = self.data_dir.join("rubric.yaml");
        if rubric_path.exists() {
            Rubric::load(&rubric_path).unwrap_or_else(|_| crate::rubric::default_rubric())
        } else {
            crate::rubric::default_rubric()
        }
    }

    fn open_store(&self) -> Result<EvidenceStore, String> {
        EvidenceStore::open(&self.data_dir).map_err(|e| format!("Failed to open store: {e}"))
    }
}

#[tool_router]
impl ImpactServer {
    /// Insert a new evidence card into the store.
    #[tool(
        name = "insert_card",
        description = "Add a new evidence card to the store"
    )]
    async fn insert_card(&self, Parameters(req): Parameters<InsertCardRequest>) -> String {
        let src = match req.source.to_lowercase().as_str() {
            "github" => EvidenceSource::Github,
            "jira" => EvidenceSource::Jira,
            "slack" => EvidenceSource::Slack,
            "gdocs" => EvidenceSource::Gdocs,
            _ => EvidenceSource::Manual,
        };

        let mut card = EvidenceCard::new(src, &req.summary);
        if let Some(l) = req.link {
            card = card.with_link(l);
        }
        if let Some(tags) = req.rubric_tags {
            card = card.with_rubric_tags(tags.split(',').map(|s| s.trim().to_string()).collect());
        }
        if let Some(tags) = req.archetype_tags {
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
        card = card.with_confidence(req.confidence);

        let mut store = match self.open_store() {
            Ok(s) => s,
            Err(e) => return format!("Error: {e}"),
        };

        match store.insert(card) {
            Ok(()) => "Evidence card added.".to_string(),
            Err(e) => format!("Error: {e}"),
        }
    }

    /// List evidence cards with optional filtering.
    #[tool(name = "list_cards", description = "List evidence cards")]
    async fn list_cards(&self, Parameters(req): Parameters<ListCardsRequest>) -> String {
        let store = match self.open_store() {
            Ok(s) => s,
            Err(e) => return format!("Error: {e}"),
        };

        let mut cards: Vec<&EvidenceCard> = if let Some(ref src) = req.source {
            let source = match src.to_lowercase().as_str() {
                "github" => EvidenceSource::Github,
                "jira" => EvidenceSource::Jira,
                "slack" => EvidenceSource::Slack,
                "gdocs" => EvidenceSource::Gdocs,
                _ => EvidenceSource::Manual,
            };
            store.by_source(source)
        } else if let Some(ref tag) = req.rubric_tag {
            store.by_rubric_tag(tag)
        } else if let Some(ref arch) = req.archetype_tag {
            let archetype = match arch.to_lowercase().as_str() {
                "tech_lead" | "techlead" => Archetype::TechLead,
                "architect" => Archetype::Architect,
                "problem_solver" | "problemsolver" => Archetype::ProblemSolver,
                "operator" => Archetype::Operator,
                "mentor" => Archetype::Mentor,
                _ => return format!("Unknown archetype: {arch}"),
            };
            store.by_archetype(archetype)
        } else {
            store.all()
        };

        if let Some(limit) = req.limit {
            cards.truncate(limit);
        }

        if cards.is_empty() {
            return "No evidence cards found.".to_string();
        }

        let mut out = format!("{} evidence card(s):\n\n", cards.len());
        for card in &cards {
            let tags = if card.rubric_tags.is_empty() {
                String::new()
            } else {
                format!(" [{}]", card.rubric_tags.join(", "))
            };
            out.push_str(&format!(
                "  [{}] {}: {} — {} (confidence: {:.0}%){tags}\n",
                card.id,
                card.source,
                card.timestamp.format("%Y-%m-%d"),
                card.summary,
                card.confidence * 100.0,
            ));
        }
        out
    }

    /// Delete an evidence card by ID.
    #[tool(name = "delete_card", description = "Delete an evidence card")]
    async fn delete_card(&self, Parameters(req): Parameters<DeleteCardRequest>) -> String {
        let id = match Uuid::parse_str(&req.id) {
            Ok(uuid) => uuid,
            Err(_) => return format!("Invalid UUID: {}", req.id),
        };

        let mut store = match self.open_store() {
            Ok(s) => s,
            Err(e) => return format!("Error: {e}"),
        };

        match store.remove(id) {
            Ok(Some(_)) => format!("Card {} deleted.", id),
            Ok(None) => format!("Card {} not found.", id),
            Err(e) => format!("Error: {e}"),
        }
    }

    /// Compute the overall readiness score and per-dimension breakdown.
    #[tool(
        name = "get_readiness",
        description = "Get readiness score and breakdown"
    )]
    async fn get_readiness(&self) -> String {
        let store = match self.open_store() {
            Ok(s) => s,
            Err(e) => return format!("Error: {e}"),
        };
        let rubric = self.load_rubric();
        let readiness = ReadinessMap::compute(&store, &rubric);

        serde_json::to_string_pretty(&readiness).unwrap_or_else(|e| format!("Error: {e}"))
    }

    /// Get the active rubric as YAML.
    #[tool(name = "get_rubric", description = "Get the active rubric")]
    async fn get_rubric(&self) -> String {
        let rubric = self.load_rubric();
        serde_yaml_ng::to_string(&rubric).unwrap_or_else(|e| format!("Error: {e}"))
    }

    /// Replace the active rubric with new YAML content.
    #[tool(name = "set_rubric", description = "Set a new active rubric")]
    async fn set_rubric(&self, Parameters(req): Parameters<SetRubricRequest>) -> String {
        let rubric: Rubric = match serde_yaml_ng::from_str(&req.yaml) {
            Ok(r) => r,
            Err(e) => return format!("Invalid YAML: {e}"),
        };

        let rubric_path = self.data_dir.join("rubric.yaml");
        match rubric.save(&rubric_path) {
            Ok(()) => format!("Rubric \"{}\" (v{}) saved.", rubric.name, rubric.version),
            Err(e) => format!("Error: {e}"),
        }
    }

    /// Pull new evidence from all configured integrations.
    #[tool(
        name = "pull_all",
        description = "Pull evidence from all configured sources"
    )]
    async fn pull_all(&self) -> String {
        use crate::integrations::{
            gdocs::GdocsConnector, github::GithubConnector, jira::JiraConnector,
            slack::SlackConnector, Connector,
        };

        let connectors: Vec<Box<dyn Connector>> = vec![
            Box::new(GithubConnector::new()),
            Box::new(JiraConnector::new()),
            Box::new(SlackConnector::new()),
            Box::new(GdocsConnector::new()),
        ];

        let mut store = match self.open_store() {
            Ok(s) => s,
            Err(e) => return format!("Error: {e}"),
        };

        let mut out = String::new();
        let mut total = 0;
        for connector in &connectors {
            if !connector.is_configured() {
                out.push_str(&format!("  [skip] {} — not configured\n", connector.name()));
                continue;
            }
            match connector.pull().await {
                Ok(cards) => {
                    let n = cards.len();
                    for card in cards {
                        if let Err(e) = store.insert(card) {
                            out.push_str(&format!("  [warn] failed to store card: {e}\n"));
                        }
                    }
                    out.push_str(&format!("  [ok]   {} — {n} card(s) pulled\n", connector.name()));
                    total += n;
                }
                Err(e) => {
                    out.push_str(&format!("  [err]  {} — {e}\n", connector.name()));
                }
            }
        }
        out.push_str(&format!("\n{total} total evidence card(s) added."));
        out
    }

    /// Pull new evidence from a specific integration source.
    #[tool(
        name = "pull_source",
        description = "Pull evidence from a specific source"
    )]
    async fn pull_source(&self, Parameters(req): Parameters<PullSourceRequest>) -> String {
        use crate::integrations::{
            gdocs::GdocsConnector, github::GithubConnector, jira::JiraConnector,
            slack::SlackConnector, Connector,
        };

        let connector: Box<dyn Connector> = match req.source.to_lowercase().as_str() {
            "github" => Box::new(GithubConnector::new()),
            "jira" => Box::new(JiraConnector::new()),
            "slack" => Box::new(SlackConnector::new()),
            "gdocs" => Box::new(GdocsConnector::new()),
            _ => return format!("Unknown source: {}", req.source),
        };

        if !connector.is_configured() {
            return format!("{} is not configured", connector.name());
        }

        let mut store = match self.open_store() {
            Ok(s) => s,
            Err(e) => return format!("Error: {e}"),
        };

        match connector.pull().await {
            Ok(cards) => {
                let n = cards.len();
                for card in cards {
                    if let Err(e) = store.insert(card) {
                        return format!("Error storing card: {e}");
                    }
                }
                format!("{} — {n} card(s) pulled", connector.name())
            }
            Err(e) => format!("Error: {e}"),
        }
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
                prompts: Some(Default::default()),
                ..Default::default()
            },
            ..Default::default()
        }
    }

    async fn list_prompts(
        &self,
        _params: Option<PaginatedRequestParam>,
        _context: RequestContext<RoleServer>,
    ) -> Result<ListPromptsResult, ErrorData> {
        Ok(ListPromptsResult {
            prompts: vec![
                Prompt {
                    name: "weekly_status".into(),
                    title: None,
                    description: Some("Draft a weekly status update from recent evidence".into()),
                    arguments: None,
                    icons: None,
                    meta: None,
                },
                Prompt {
                    name: "packet_draft".into(),
                    title: None,
                    description: Some(
                        "Generate a promotion packet from all evidence and rubric".into(),
                    ),
                    arguments: None,
                    icons: None,
                    meta: None,
                },
                Prompt {
                    name: "gap_analysis".into(),
                    title: None,
                    description: Some("Analyze readiness gaps and suggest priorities".into()),
                    arguments: None,
                    icons: None,
                    meta: None,
                },
                Prompt {
                    name: "archetype_review".into(),
                    title: None,
                    description: Some("Review archetype strengths and provide coaching".into()),
                    arguments: None,
                    icons: None,
                    meta: None,
                },
                Prompt {
                    name: "readiness_check".into(),
                    title: None,
                    description: Some("Detailed readiness breakdown with scoring".into()),
                    arguments: None,
                    icons: None,
                    meta: None,
                },
            ],
            ..Default::default()
        })
    }

    async fn get_prompt(
        &self,
        params: GetPromptRequestParam,
        _context: RequestContext<RoleServer>,
    ) -> Result<GetPromptResult, ErrorData> {
        let store = self
            .open_store()
            .map_err(|e| ErrorData::internal_error(e, None))?;
        let rubric = self.load_rubric();

        let content = match params.name.as_str() {
            "weekly_status" => {
                let mut cards = store.all();
                cards.sort_by(|a, b| b.timestamp.cmp(&a.timestamp));
                let recent: Vec<_> = cards.into_iter().take(10).collect();

                let readiness = ReadinessMap::compute(&store, &rubric);

                let mut out = String::from("# Weekly Status Update Context\n\n");
                out.push_str(&format!(
                    "Overall readiness: {:.0}%\n\n",
                    readiness.overall_score * 100.0
                ));
                out.push_str("## Recent Evidence (last 10 items):\n\n");
                for card in recent {
                    out.push_str(&format!(
                        "- [{}] {} — {}\n",
                        card.source,
                        card.timestamp.format("%Y-%m-%d"),
                        card.summary
                    ));
                }
                out.push_str("\n## Instructions\n\n");
                out.push_str(
                    "Draft a concise weekly status update highlighting key accomplishments. \
                     Focus on impact and outcomes, not just tasks completed. \
                     \n\n**CRITICAL for Staff+ level:**\
                     \n- Emphasize the BUSINESS IMPACT and company importance of work, not just technical quality\
                     \n- Highlight work you identified and drove proactively, not just assigned tasks\
                     \n- Explain WHY projects matter to the company/customers, not just WHAT you did\
                     \n- Staff+ engineers identify important problems and drive solutions; doing a great job on \
                     trivial work isn't enough\
                     \n\nRemember: Promotion requires 6-12 months of sustained performance at this level. \
                     Status updates should demonstrate consistent pattern of identifying and driving \
                     high-impact work.",
                );
                out
            }
            "packet_draft" => {
                let cards = store.all();
                let readiness = ReadinessMap::compute(&store, &rubric);

                let mut out = String::from("# Promotion Packet Context\n\n");
                out.push_str(&format!(
                    "Rubric: {} (v{})\n",
                    rubric.name, rubric.version
                ));
                out.push_str(&format!(
                    "Overall readiness: {:.0}%\n\n",
                    readiness.overall_score * 100.0
                ));

                out.push_str("## Evidence by Dimension\n\n");
                for dim in &rubric.dimensions {
                    let dim_cards = store.by_rubric_tag(&dim.key);
                    let level = readiness
                        .dimension_coverage
                        .get(&dim.key)
                        .copied()
                        .unwrap_or(CoverageLevel::None);
                    out.push_str(&format!("### {} [{}]\n", dim.label, level));
                    for card in dim_cards.iter().take(5) {
                        out.push_str(&format!("- {}\n", card.summary));
                    }
                    out.push('\n');
                }

                out.push_str(&format!("\nTotal evidence items: {}\n\n", cards.len()));
                out.push_str("## Staff+ Level Expectations\n\n");
                out.push_str(
                    "**Project Selection & Initiative:**\
                     \n- Staff+ roles require working on IMPORTANT problems, not just any problems well\
                     \n- Engineers at this level identify critical issues and drive progress on them\
                     \n- Executing excellently on assigned/trivial work is insufficient\
                     \n- Must demonstrate proactive problem-finding and solution-driving\n\n",
                );
                out.push_str("## Timeline Context\n\n");
                out.push_str(
                    "Promotion typically requires 6-12 months of sustained performance at the next level. \
                     This packet should demonstrate consistent impact over time, not just recent achievements.\n\n",
                );
                out.push_str("## Instructions\n\n");
                out.push_str(
                    "Generate a comprehensive promotion packet narrative. Organize by rubric dimensions, \
                     emphasize concrete outcomes and scope, and tell a compelling story of sustained growth.\
                     \n\n**CRITICAL:** For each major piece of evidence:\
                     \n- Articulate the business/company importance clearly — WHY it mattered\
                     \n- Distinguish between assigned work vs. proactively identified opportunities\
                     \n- Highlight scope and impact, not just technical excellence\
                     \n- Show pattern of identifying important problems and driving solutions\
                     \n\nA strong packet demonstrates you're already operating as a Staff+ engineer: \
                     finding what matters and making it happen, not just executing well on what's given.",
                );
                out
            }
            "gap_analysis" => {
                let readiness = ReadinessMap::compute(&store, &rubric);

                let mut out = String::from("# Gap Analysis Context\n\n");
                out.push_str(&format!(
                    "Overall readiness: {:.0}%\n\n",
                    readiness.overall_score * 100.0
                ));

                if readiness.gaps.is_empty() {
                    out.push_str("No gaps identified — all dimensions have strong coverage!\n\n");
                } else {
                    out.push_str("## Identified Gaps\n\n");
                    for gap in &readiness.gaps {
                        out.push_str(&format!(
                            "- {} ({}): {}\n",
                            gap.dimension, gap.coverage, gap.suggestion
                        ));
                    }
                    out.push('\n');
                }

                out.push_str("## Instructions\n\n");
                out.push_str(
                    "Analyze the gaps and provide a prioritized action plan. Suggest concrete steps \
                     to close each gap and estimate effort/timeline for addressing them.\
                     \n\n**IMPORTANT - Staff+ Level Considerations:**\
                     \n- Gaps aren't just about demonstrating skills — they're about finding HIGH-IMPACT \
                     opportunities to apply those skills\
                     \n- Suggest projects/problems that are both gap-closing AND important to the company\
                     \n- Emphasize proactive identification of work, not just executing assigned tasks well\
                     \n- \"Doing more of what you're told\" won't close gaps; \"identifying what matters and \
                     driving it\" will\
                     \n\nTimeline: Promotion requires 6-12 months of sustained performance. Prioritize gaps \
                     that can be closed through repeated demonstrations of proactive, high-impact work over \
                     the coming months.",
                );
                out
            }
            "archetype_review" => {
                let profile = infer_archetype_profile(&store);
                let readiness = ReadinessMap::compute(&store, &rubric);

                let mut out = String::from("# Archetype Review Context\n\n");
                out.push_str("## Current Archetype Strengths\n\n");
                for (arch, strength) in &profile.inferred {
                    out.push_str(&format!(
                        "- {} ({:.0}%): {}\n",
                        arch.label(),
                        strength * 100.0,
                        arch.description()
                    ));
                }
                out.push('\n');

                if let Some(dominant) = profile.dominant() {
                    out.push_str(&format!(
                        "Dominant archetype: {} ({:.0}%)\n\n",
                        dominant.label(),
                        readiness
                            .archetype_strength
                            .get(dominant.label())
                            .copied()
                            .unwrap_or(0.0)
                            * 100.0
                    ));
                }

                out.push_str("## Instructions\n\n");
                out.push_str(
                    "Review the archetype profile and provide coaching on: (1) which archetypes to lean into, \
                     (2) how to develop underrepresented archetypes if valuable, and (3) how to showcase \
                     archetype strengths in narratives and status updates.",
                );
                out
            }
            "readiness_check" => {
                let readiness = ReadinessMap::compute(&store, &rubric);

                let mut out = String::from("# Readiness Check Context\n\n");
                out.push_str(&format!(
                    "Overall score: {:.0}%\n\n",
                    readiness.overall_score * 100.0
                ));

                out.push_str("## Dimension Breakdown\n\n");
                for dim in &rubric.dimensions {
                    let level = readiness
                        .dimension_coverage
                        .get(&dim.key)
                        .copied()
                        .unwrap_or(CoverageLevel::None);
                    out.push_str(&format!(
                        "- {}: {} (weight: {:.1})\n",
                        dim.label, level, dim.weight
                    ));
                }
                out.push('\n');

                out.push_str("## Archetype Strengths\n\n");
                for arch in Archetype::ALL {
                    let strength = readiness
                        .archetype_strength
                        .get(arch.label())
                        .copied()
                        .unwrap_or(0.0);
                    out.push_str(&format!("- {}: {:.0}%\n", arch.label(), strength * 100.0));
                }
                out.push('\n');

                out.push_str("## Staff+ Level Context\n\n");
                out.push_str(
                    "At Staff+ level, HOW you build evidence matters:\
                     \n- High scores on trivial work = not ready\
                     \n- Evidence of executing assigned work well = insufficient\
                     \n- Evidence of IDENTIFYING important problems and DRIVING solutions = Staff+ behavior\
                     \n\nReadiness measures behaviors, but the work itself must be high-impact.\n\n",
                );
                out.push_str("## Timeline Context\n\n");
                out.push_str(
                    "Promotion readiness isn't just about hitting a score — it requires 6-12 months of \
                     sustained performance at the next level. A high readiness score today means you're \
                     demonstrating the right behaviors; maintaining it over months proves consistency.\n\n",
                );
                out.push_str("## Instructions\n\n");
                out.push_str(
                    "Provide a detailed readiness assessment. Explain what each dimension score means, \
                     how the overall score is calculated, and what concrete next steps would most improve readiness.\
                     \n\nIMPORTANT: Frame recommendations emphasizing:\
                     \n- Project selection: Choose high-impact work, not just any work\
                     \n- Proactive initiative: Identify problems, don't just respond to tickets\
                     \n- Business impact: Make company/customer importance clear in all communications\
                     \n- Timeline: Build evidence consistently over 6-12 months",
                );
                out
            }
            _ => {
                return Err(ErrorData::invalid_params(
                    format!("Unknown prompt: {}", params.name),
                    None,
                ))
            }
        };

        Ok(GetPromptResult {
            description: None,
            messages: vec![PromptMessage {
                role: PromptMessageRole::User,
                content: rmcp::model::PromptMessageContent::Text {
                    text: content,
                },
            }],
        })
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
