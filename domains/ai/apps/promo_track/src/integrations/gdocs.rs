use std::future::Future;
use std::pin::Pin;

use crate::evidence::{EvidenceCard, EvidenceSource};
use crate::mcp::McpClient;

use super::{Connector, ConnectorError};

/// Google Docs integration via MCP.
///
/// Spawns the `google-docs-mcp` server as a child process and uses
/// MCP tool calls to search for and read design docs, RFCs,
/// post-mortems, and other long-form technical writing.
///
/// Requires:
/// - `STAFFTRACK_GDOCS_MCP_CMD`: path to the MCP server entry point
///   (default: `npx`)
/// - `STAFFTRACK_GDOCS_MCP_ARGS`: arguments to pass (default:
///   `google-docs-mcp`)
/// - The MCP server must be installed and configured with Google
///   OAuth credentials independently.
pub struct GdocsConnector {
    mcp_cmd: String,
    mcp_args: Vec<String>,
}

impl GdocsConnector {
    pub fn new() -> Self {
        let mcp_cmd = std::env::var("STAFFTRACK_GDOCS_MCP_CMD")
            .unwrap_or_else(|_| "npx".to_string());
        let mcp_args = std::env::var("STAFFTRACK_GDOCS_MCP_ARGS")
            .unwrap_or_else(|_| "google-docs-mcp".to_string())
            .split_whitespace()
            .map(String::from)
            .collect();
        Self { mcp_cmd, mcp_args }
    }

    async fn pull_via_mcp(&self) -> Result<Vec<EvidenceCard>, ConnectorError> {
        let args: Vec<&str> = self.mcp_args.iter().map(|s| s.as_str()).collect();
        let client = McpClient::spawn(&self.mcp_cmd, &args)
            .await
            .map_err(|e| ConnectorError::Mcp(e.to_string()))?;

        tracing::info!("Google Docs MCP server connected");

        // Discover available tools
        let tools = client
            .list_tools()
            .await
            .map_err(|e| ConnectorError::Mcp(e.to_string()))?;
        tracing::debug!("Google Docs MCP tools: {tools:?}");

        let mut cards = Vec::new();

        // Pull recently modified docs (design docs, RFCs, post-mortems)
        if tools.iter().any(|t| t == "getRecentGoogleDocs") {
            match client
                .call_tool("getRecentGoogleDocs", serde_json::json!({"maxResults": 20}))
                .await
            {
                Ok(response) => {
                    let parsed: Vec<DocSummary> = serde_json::from_str(&response)
                        .unwrap_or_default();
                    for doc in parsed {
                        let mut card = EvidenceCard::new(EvidenceSource::Gdocs, &doc.title)
                            .with_confidence(0.6);
                        if let Some(link) = doc.link {
                            card = card.with_link(link);
                        }
                        cards.push(card);
                    }
                }
                Err(e) => {
                    tracing::warn!("getRecentGoogleDocs failed: {e}");
                }
            }
        }

        // Search for docs matching promotion-relevant keywords
        if tools.iter().any(|t| t == "searchGoogleDocs") {
            for keyword in &["RFC", "design doc", "post-mortem", "architecture"] {
                match client
                    .call_tool(
                        "searchGoogleDocs",
                        serde_json::json!({"query": keyword, "maxResults": 5}),
                    )
                    .await
                {
                    Ok(response) => {
                        let parsed: Vec<DocSummary> = serde_json::from_str(&response)
                            .unwrap_or_default();
                        for doc in parsed {
                            // Avoid duplicates by link
                            if doc.link.as_ref().is_some_and(|l| {
                                cards.iter().any(|c| c.link.as_ref() == Some(l))
                            }) {
                                continue;
                            }
                            let mut card = EvidenceCard::new(
                                EvidenceSource::Gdocs,
                                &doc.title,
                            )
                            .with_confidence(0.5)
                            .with_rubric_tags(vec!["scope".into(), "quality".into()]);
                            if let Some(link) = doc.link {
                                card = card.with_link(link);
                            }
                            cards.push(card);
                        }
                    }
                    Err(e) => {
                        tracing::warn!("searchGoogleDocs({keyword}) failed: {e}");
                    }
                }
            }
        }

        // Read content of the most recent docs to extract richer evidence
        if tools.iter().any(|t| t == "readGoogleDoc") {
            for card in cards.iter_mut().take(5) {
                if let Some(link) = &card.link {
                    // Extract doc ID from link — typically the last path segment
                    if let Some(doc_id) = extract_doc_id(link) {
                        match client
                            .call_tool(
                                "readGoogleDoc",
                                serde_json::json!({
                                    "documentId": doc_id,
                                    "format": "markdown"
                                }),
                            )
                            .await
                        {
                            Ok(content) => {
                                // Use first 500 chars as an excerpt
                                let excerpt: String = content.chars().take(500).collect();
                                if !excerpt.is_empty() {
                                    card.excerpts.push(excerpt);
                                    card.confidence = 0.8;
                                }
                            }
                            Err(e) => {
                                tracing::debug!("readGoogleDoc({doc_id}) failed: {e}");
                            }
                        }
                    }
                }
            }
        }

        if let Err(e) = client.shutdown().await {
            tracing::warn!("MCP shutdown: {e}");
        }

        tracing::info!("Google Docs: pulled {} evidence cards", cards.len());
        Ok(cards)
    }
}

impl Connector for GdocsConnector {
    fn name(&self) -> &str {
        "Google Docs (MCP)"
    }

    fn is_configured(&self) -> bool {
        // We consider the connector configured if the user has explicitly
        // set the MCP command env var, or if the default `npx` is on PATH.
        std::env::var("STAFFTRACK_GDOCS_MCP_CMD").is_ok()
            || std::process::Command::new(&self.mcp_cmd)
                .arg("--version")
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null())
                .status()
                .is_ok()
    }

    fn pull(&self) -> Pin<Box<dyn Future<Output = Result<Vec<EvidenceCard>, ConnectorError>> + Send + '_>> {
        Box::pin(self.pull_via_mcp())
    }
}

/// Minimal representation of a doc from search/list results.
#[derive(serde::Deserialize, Default)]
struct DocSummary {
    #[serde(default)]
    title: String,
    #[serde(default)]
    link: Option<String>,
}

/// Extract a Google Doc ID from a URL like
/// `https://docs.google.com/document/d/<ID>/edit`
fn extract_doc_id(url: &str) -> Option<&str> {
    let marker = "/document/d/";
    let start = url.find(marker)? + marker.len();
    let rest = &url[start..];
    let end = rest.find('/').unwrap_or(rest.len());
    let id = &rest[..end];
    if id.is_empty() { None } else { Some(id) }
}
