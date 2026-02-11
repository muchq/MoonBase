use std::path::PathBuf;

use clap::{Parser, Subcommand};

#[derive(Parser)]
#[command(
    name = "impact-mcp",
    about = "impact-mcp — amplify your impact and make it visible",
    version = "0.0.8-alpha",
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

    /// Manage tracked projects.
    Projects {
        #[command(subcommand)]
        subcommand: ProjectsCommand,
    },

    /// Generate a weekly update summary template.
    WeeklyUpdate,

    /// Pull new evidence from configured integrations.
    Pull {
        /// Use Claude to pull evidence (via MCP).
        #[arg(long)]
        claude: bool,

        /// Use Codex to pull evidence (via MCP).
        #[arg(long)]
        codex: bool,
    },

    /// Start as an MCP server over stdio.
    Serve,

    /// Set up skills and MCP configuration.
    Setup {
        /// Install Claude skills to this directory.
        #[arg(long)]
        claude_skills_dir: Option<PathBuf>,

        /// Install Codex skills to this directory.
        #[arg(long)]
        codex_skills_dir: Option<PathBuf>,
    },

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

#[derive(Subcommand)]
pub enum ProjectsCommand {
    /// List all tracked projects.
    List,

    /// Add a new project.
    Add {
        /// Name of the project.
        #[arg(long)]
        name: String,

        /// Your role in the project.
        #[arg(long)]
        role: String,

        /// Related Jira projects (comma-separated).
        #[arg(long)]
        jira: Option<String>,

        /// Related Git repositories (comma-separated).
        #[arg(long)]
        repos: Option<String>,

        /// Status of the project (e.g. "Active", "Planning", "Done").
        #[arg(long, default_value = "Active")]
        status: String,

        /// Completion percentage (0.0 - 1.0).
        #[arg(long, default_value = "0.0")]
        completion: f64,
    },

    /// Update an existing project.
    Update {
        /// Name of the project to update.
        #[arg(long)]
        name: String,

        /// New role in the project.
        #[arg(long)]
        role: Option<String>,

        /// New status.
        #[arg(long)]
        status: Option<String>,

        /// New completion percentage (0.0 - 1.0).
        #[arg(long)]
        completion: Option<f64>,

        /// New Jira projects (comma-separated).
        #[arg(long)]
        jira: Option<String>,

        /// New Git repositories (comma-separated).
        #[arg(long)]
        repos: Option<String>,
    },

    /// Remove a project.
    Remove {
        /// Name of the project to remove.
        #[arg(long)]
        name: String,
    },
}
