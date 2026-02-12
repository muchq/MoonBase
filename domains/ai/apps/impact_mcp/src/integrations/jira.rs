use std::future::Future;
use std::pin::Pin;

use crate::evidence::{EvidenceCard, EvidenceSource};
use serde::Deserialize;

use super::{Connector, ConnectorError};

/// Jira integration — reads epic ownership, cross-team tickets, and
/// project-level scope indicators.
pub struct JiraConnector {
    token: Option<String>,
    email: Option<String>,
    base_url: Option<String>,
}

impl JiraConnector {
    pub fn new() -> Self {
        Self {
            token: std::env::var("IMPACT_MCP_JIRA_TOKEN").ok(),
            email: std::env::var("IMPACT_MCP_JIRA_EMAIL").ok(),
            base_url: std::env::var("IMPACT_MCP_JIRA_URL").ok(),
        }
    }
}

#[derive(Deserialize)]
struct JiraSearchResponse {
    #[serde(default)]
    issues: Vec<JiraIssue>,
}

#[derive(Deserialize)]
struct JiraIssue {
    key: String,
    fields: JiraFields,
}

#[derive(Deserialize)]
struct JiraFields {
    summary: String,
    description: Option<serde_json::Value>,
    updated: String,
    status: JiraStatus,
    #[serde(default)]
    comment: Option<JiraComments>,
}

#[derive(Deserialize)]
struct JiraStatus {
    name: String,
}

#[derive(Deserialize)]
struct JiraComments {
    total: i32,
}

impl Connector for JiraConnector {
    fn name(&self) -> &str {
        "Jira"
    }

    fn is_configured(&self) -> bool {
        self.token.is_some() && self.base_url.is_some()
    }

    fn pull(&self) -> Pin<Box<dyn Future<Output = Result<(Vec<EvidenceCard>, Vec<String>), ConnectorError>> + Send + '_>> {
        Box::pin(async move {
            if !self.is_configured() {
                return Err(ConnectorError::NotConfigured(
                    "Set IMPACT_MCP_JIRA_TOKEN and IMPACT_MCP_JIRA_URL to enable Jira integration".into(),
                ));
            }

            let token = self.token.as_ref().unwrap();
            let base_url = self.base_url.as_ref().unwrap();

            let client = reqwest::Client::new();
            // Try v3 search first, fallback logic isn't strictly needed if we just use v3 endpoint
            let mut request = client.get(format!("{}/rest/api/3/search", base_url))
                .query(&[
                    ("jql", "assignee = currentUser() AND updated >= -30d ORDER BY updated DESC"),
                    ("fields", "summary,description,updated,status,comment"),
                    ("maxResults", "50"),
                ]);

            if let Some(email) = &self.email {
                request = request.basic_auth(email, Some(token));
            } else {
                request = request.bearer_auth(token);
            }

            let resp = request.send().await.map_err(|e| ConnectorError::Network(e.to_string()))?;

            if !resp.status().is_success() {
                return Err(ConnectorError::Network(format!("Jira API error: {}", resp.status())));
            }

            let search_results: JiraSearchResponse = resp.json().await.map_err(|e| ConnectorError::Parse(e.to_string()))?;

            let mut cards = Vec::new();
            let mut warnings = Vec::new();

            for issue in search_results.issues {
                let key = &issue.key;
                let summary = &issue.fields.summary;
                let status = &issue.fields.status.name;

                // Description check
                let has_description = match &issue.fields.description {
                    Some(serde_json::Value::String(s)) => !s.trim().is_empty(),
                    Some(serde_json::Value::Object(map)) => !map.is_empty(),
                    // For ADF (Atlassian Document Format), it is an object with "content" array
                    _ => false,
                };

                if !has_description {
                    warnings.push(format!("Issue {} has no description. Please add details about the work.", key));
                }

                // Stale check
                if let Ok(updated_time) = chrono::DateTime::parse_from_str(&issue.fields.updated, "%Y-%m-%dT%H:%M:%S%.3f%z") {
                    let now = chrono::Utc::now();
                    let age = now.signed_duration_since(updated_time);
                    if age.num_days() > 14 && status != "Done" && status != "Closed" && status != "Resolved" {
                        warnings.push(format!("Issue {} is stale (no updates in {} days).", key, age.num_days()));
                    }
                }

                // Comment check (optional, but requested by user: "If the JIRA tickets don't have updates or comments...")
                if let Some(comments) = &issue.fields.comment {
                    if comments.total == 0 && age_days(&issue.fields.updated) > 7 {
                         warnings.push(format!("Issue {} has no comments. Add updates to track progress.", key));
                    }
                }

                let card = EvidenceCard::new(
                    EvidenceSource::Jira,
                    &format!("{}: {} ({})", key, summary, status),
                ).with_link(format!("{}/browse/{}", base_url, key));

                cards.push(card);
            }

            Ok((cards, warnings))
        })
    }
}

fn age_days(date_str: &str) -> i64 {
    if let Ok(updated_time) = chrono::DateTime::parse_from_str(date_str, "%Y-%m-%dT%H:%M:%S%.3f%z") {
        let now = chrono::Utc::now();
        now.signed_duration_since(updated_time).num_days()
    } else {
        0
    }
}
