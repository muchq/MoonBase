use std::path::PathBuf;

use clap::{Parser, Subcommand};

#[derive(Parser)]
#[command(
    name = "stafftrack",
    about = "StaffTrack — make Staff-level impact visible, actionable, and promotable",
    version,
    long_about = "A local-first AI agent that helps senior engineers capture Staff-level \
                   impact from everyday work artifacts, understand promotion expectations, \
                   maintain a promotion packet, and draft high-quality status updates."
)]
pub struct Cli {
    /// Path to the StaffTrack data directory.
    #[arg(long, env = "STAFFTRACK_DATA_DIR")]
    pub data_dir: Option<PathBuf>,

    #[command(subcommand)]
    pub command: Command,
}

#[derive(Subcommand)]
pub enum Command {
    /// Start an interactive chat session with the StaffTrack agent.
    Chat,

    /// Draft a weekly status update from recent evidence.
    Status,

    /// Generate or update your promotion packet draft.
    Packet,

    /// Show your current readiness score and dimension breakdown.
    Readiness,

    /// List the supported Staff archetypes.
    Archetypes,

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
