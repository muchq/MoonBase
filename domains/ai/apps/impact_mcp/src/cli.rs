use std::path::PathBuf;

use clap::{Parser, Subcommand};

#[derive(Parser)]
#[command(
    name = "impact-mcp",
    about = "impact-mcp — amplify your impact and make it visible",
    version,
    long_about = "A local-first AI agent that helps engineers capture evidence of impact, \
                   understand role expectations, close gaps, and communicate contributions \
                   clearly — for better project results and growth in your career."
)]
pub struct Cli {
    /// Path to the impact-mcp data directory.
    #[arg(long, env = "IMPACT_MCP_DATA_DIR")]
    pub data_dir: Option<PathBuf>,

    #[command(subcommand)]
    pub command: Command,
}

#[derive(Subcommand)]
pub enum Command {
    /// Manage your role rubric.
    Rubric {
        #[command(subcommand)]
        subcommand: RubricCommand,
    },

    /// Manage evidence cards.
    Evidence {
        #[command(subcommand)]
        subcommand: EvidenceCommand,
    },

    /// Pull new evidence from configured integrations.
    Pull,

    /// Start as an MCP server over stdio.
    Serve,

    /// Set up Claude integration (commands and MCP server config).
    Setup,

    /// Set up automatic hourly evidence pulls (macOS only).
    SetupCron,
}

#[derive(Subcommand)]
pub enum RubricCommand {
    /// Display the active rubric.
    Show,

    /// Write the default rubric to the data directory.
    Init,

    /// Load a rubric from a YAML file.
    Load {
        /// Path to the rubric YAML file.
        path: PathBuf,
    },
}

#[derive(Subcommand)]
pub enum EvidenceCommand {
    /// List all evidence cards.
    List,

    /// Add a new evidence card manually.
    Add {
        /// Summary of the evidence (1-3 sentences).
        #[arg(long)]
        summary: String,

        /// Source: github, jira, slack, gdocs, or manual.
        #[arg(long, default_value = "manual")]
        source: String,

        /// URL or reference link.
        #[arg(long)]
        link: Option<String>,

        /// Comma-separated rubric dimension tags (e.g. "scope,leverage").
        #[arg(long)]
        rubric_tags: Option<String>,

        /// Comma-separated archetype tags (e.g. "tech_lead,architect").
        #[arg(long)]
        archetype_tags: Option<String>,
    },
}
