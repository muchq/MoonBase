use std::path::PathBuf;
use std::process;

use chrono::{Duration, Local, Utc};
use clap::Parser;
use tracing_subscriber::EnvFilter;

use rmcp::ServiceExt;

use impact_mcp::archetype::Archetype;
use impact_mcp::evidence::{EvidenceCard, EvidenceSource, EvidenceStore};
use impact_mcp::integrations::Connector;
use impact_mcp::projects::ProjectStore;
use impact_mcp::prompts::generate_pull_prompt;
use impact_mcp::cli::{self, Cli, Command};
use impact_mcp::rubric::{self, Rubric};
use impact_mcp::server::ImpactServer;

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::from_default_env())
        .init();

    let cli = Cli::parse();

    let data_dir = cli.data_dir.unwrap_or_else(default_data_dir);
    if let Err(e) = std::fs::create_dir_all(&data_dir) {
        eprintln!("error: cannot create data directory {}: {e}", data_dir.display());
        process::exit(1);
    }

    match cli.command {
        Command::Rubric { subcommand } => run_rubric(&data_dir, subcommand),
        Command::Evidence { subcommand } => run_evidence(&data_dir, subcommand),
        Command::Projects { subcommand } => run_projects(&data_dir, subcommand),
        Command::WeeklyUpdate => run_weekly_update(&data_dir),
        Command::Pull { claude, codex } => run_pull(&data_dir, claude, codex).await,
        Command::Serve => run_serve(&data_dir).await,
        Command::Setup {
            claude_skills_dir,
            codex_skills_dir,
        } => run_setup(claude_skills_dir, codex_skills_dir),
        Command::SetupCron => run_setup_cron(),
    }
}

fn default_data_dir() -> PathBuf {
    dirs::data_local_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join("impact-mcp")
}

fn load_rubric(data_dir: &PathBuf) -> Rubric {
    let rubric_path = data_dir.join("rubric.yaml");
    if rubric_path.exists() {
        Rubric::load(&rubric_path).unwrap_or_else(|e| {
            eprintln!("warning: failed to load rubric, using default: {e}");
            rubric::default_rubric()
        })
    } else {
        let r = rubric::default_rubric();
        if let Err(e) = r.save(&rubric_path) {
            eprintln!("warning: failed to save default rubric: {e}");
        }
        r
    }
}

fn run_rubric(data_dir: &PathBuf, sub: cli::RubricCommand) {
    match sub {
        cli::RubricCommand::Show => {
            let rubric = load_rubric(data_dir);
            println!("Active rubric: {} (v{})\n", rubric.name, rubric.version);
            for dim in &rubric.dimensions {
                println!(
                    "  {:<12} (weight {:.1}): {}",
                    dim.label, dim.weight, dim.description
                );
            }
        }
        cli::RubricCommand::Init => {
            let rubric = rubric::default_rubric();
            let path = data_dir.join("rubric.yaml");
            match rubric.save(&path) {
                Ok(()) => println!("Default rubric written to {}", path.display()),
                Err(e) => {
                    eprintln!("error: {e}");
                    process::exit(1);
                }
            }
        }
        cli::RubricCommand::Load { path } => match Rubric::load(&path) {
            Ok(r) => {
                let dest = data_dir.join("rubric.yaml");
                match r.save(&dest) {
                    Ok(()) => println!("Loaded rubric \"{}\" (v{})", r.name, r.version),
                    Err(e) => {
                        eprintln!("error saving rubric: {e}");
                        process::exit(1);
                    }
                }
            }
            Err(e) => {
                eprintln!("error loading rubric from {}: {e}", path.display());
                process::exit(1);
            }
        },
    }
}

fn run_evidence(data_dir: &PathBuf, sub: cli::EvidenceCommand) {
    match sub {
        cli::EvidenceCommand::List => {
            let store = EvidenceStore::open(data_dir).unwrap_or_else(|e| {
                eprintln!("error: {e}");
                process::exit(1);
            });
            let cards = store.all();
            if cards.is_empty() {
                println!("No evidence cards yet. Use `impact-mcp evidence add` or `impact-mcp pull`.");
                return;
            }
            println!("{} evidence card(s):\n", cards.len());
            for card in &cards {
                println!(
                    "  [{}] {} — {} (confidence: {:.0}%)",
                    card.source,
                    card.timestamp.format("%Y-%m-%d"),
                    card.summary,
                    card.confidence * 100.0,
                );
            }
        }
        cli::EvidenceCommand::Add {
            summary,
            source,
            link,
            rubric_tags,
            archetype_tags,
        } => {
            let mut store = EvidenceStore::open(data_dir).unwrap_or_else(|e| {
                eprintln!("error: {e}");
                process::exit(1);
            });

            let src = match source.to_lowercase().as_str() {
                "github" => EvidenceSource::Github,
                "jira" => EvidenceSource::Jira,
                "slack" => EvidenceSource::Slack,
                "gdocs" => EvidenceSource::Gdocs,
                _ => EvidenceSource::Manual,
            };

            let mut card = EvidenceCard::new(src, &summary);
            if let Some(l) = link {
                card = card.with_link(l);
            }
            if let Some(tags) = rubric_tags {
                card = card.with_rubric_tags(tags.split(',').map(|s| s.trim().to_string()).collect());
            }
            if let Some(tags) = archetype_tags {
                let archetypes: Vec<Archetype> = tags
                    .split(',')
                    .filter_map(|s| match s.trim().to_lowercase().as_str() {
                        "tech_lead" | "techlead" => Some(Archetype::TechLead),
                        "architect" => Some(Archetype::Architect),
                        "problem_solver" | "problemsolver" => Some(Archetype::ProblemSolver),
                        "operator" => Some(Archetype::Operator),
                        "mentor" => Some(Archetype::Mentor),
                        "right_hand" | "righthand" => Some(Archetype::RightHand),
                        "glue" => Some(Archetype::Glue),
                        _ => None,
                    })
                    .collect();
                card = card.with_archetype_tags(archetypes);
            }

            match store.insert(card) {
                Ok(()) => println!("Evidence card added."),
                Err(e) => {
                    eprintln!("error: {e}");
                    process::exit(1);
                }
            }
        }
    }
}

fn run_projects(data_dir: &PathBuf, sub: cli::ProjectsCommand) {
    let mut store = ProjectStore::open(data_dir).unwrap_or_else(|e| {
        eprintln!("error: {e}");
        process::exit(1);
    });

    if let Err(e) =
        impact_mcp::projects::handle_project_command(&mut store, sub, &mut std::io::stdout())
    {
        eprintln!("error: {e}");
        process::exit(1);
    }
}

fn run_weekly_update(data_dir: &PathBuf) {
    let pstore = ProjectStore::open(data_dir).unwrap_or_else(|e| {
        eprintln!("error opening project store: {e}");
        process::exit(1);
    });

    println!("# Weekly Update");
    println!("Date: {}\n", Local::now().format("%Y-%m-%d"));

    let projects = pstore.all();
    if projects.is_empty() {
        println!("_(No tracked projects)_");
    } else {
        for project in projects {
            println!("## {} ({})", project.name, project.role);
            println!(
                "**Status:** {} ({:.0}% complete)",
                project.status,
                project.completion * 100.0
            );
            println!("**Highlights:**");
            println!("* ");
            println!("**Blockers:**");
            println!("* None\n");
        }
    }

    // Recent evidence
    if let Ok(estore) = EvidenceStore::open(data_dir) {
        let now = Utc::now();
        let week_ago = now - Duration::days(7);
        let mut cards: Vec<_> = estore
            .all()
            .into_iter()
            .filter(|c| c.timestamp >= week_ago)
            .collect();
        // Sort by timestamp descending
        cards.sort_by(|a, b| b.timestamp.cmp(&a.timestamp));

        if !cards.is_empty() {
            println!("## Recent Evidence (Last 7 Days)");
            for card in cards {
                println!("* [{}] {}", card.source, card.summary);
            }
            println!();
        }
    }
}

async fn run_pull(data_dir: &PathBuf, claude: bool, codex: bool) {
    if claude || codex {
        let binary = if claude { "claude" } else { "codex" };

        let store_opt = ProjectStore::open(data_dir).ok();
        let projects = if let Some(ref store) = store_opt {
            store.all()
        } else {
            Vec::new()
        };

        let prompt = generate_pull_prompt(&projects);

        println!("Invoking {} with prompt...\n", binary);

        let status = std::process::Command::new(binary).arg(prompt).status();

        match status {
            Ok(s) => {
                if !s.success() {
                    eprintln!("{} exited with status: {}", binary, s);
                }
            }
            Err(e) => {
                eprintln!("Failed to run {}: {}", binary, e);
                eprintln!("Make sure it is installed and in your PATH.");
            }
        }
        return;
    }

    use impact_mcp::integrations::{
        github::GithubConnector, jira::JiraConnector, slack::SlackConnector,
    };

    let connectors: Vec<Box<dyn Connector>> = vec![
        Box::new(GithubConnector::new()),
        Box::new(JiraConnector::new()),
        Box::new(SlackConnector::new()),
    ];

    let mut store = EvidenceStore::open(data_dir).unwrap_or_else(|e| {
        eprintln!("error: {e}");
        process::exit(1);
    });

    let mut total = 0;
    for connector in &connectors {
        if !connector.is_configured() {
            println!("  [skip] {} — not configured", connector.name());
            continue;
        }
        match connector.pull().await {
            Ok(cards) => {
                let n = cards.len();
                for card in cards {
                    if let Err(e) = store.insert(card) {
                        eprintln!("  [warn] failed to store card: {e}");
                    }
                }
                println!("  [ok]   {} — {n} card(s) pulled", connector.name());
                total += n;
            }
            Err(e) => {
                eprintln!("  [err]  {} — {e}", connector.name());
            }
        }
    }
    println!("\n{total} total evidence card(s) added.");
}

fn run_setup_cron() {
    #[cfg(not(target_os = "macos"))]
    {
        eprintln!("error: setup-cron is only supported on macOS");
        eprintln!("For other platforms, set up a cron job manually:");
        eprintln!("  0 * * * * impact-mcp pull");
        process::exit(1);
    }

    #[cfg(target_os = "macos")]
    {
        use std::fs;
        println!("Setting up automatic hourly evidence pulls...\n");

        let home = dirs::home_dir().unwrap_or_else(|| {
            eprintln!("error: cannot determine home directory");
            process::exit(1);
        });

        let launch_agents_dir = home.join("Library/LaunchAgents");
        if let Err(e) = fs::create_dir_all(&launch_agents_dir) {
            eprintln!("error: cannot create ~/Library/LaunchAgents/: {e}");
            process::exit(1);
        }

        // Get the plist template
        let exe_path = std::env::current_exe().unwrap_or_else(|e| {
            eprintln!("error: cannot determine executable path: {e}");
            process::exit(1);
        });
        let exe_dir = exe_path.parent().unwrap_or_else(|| {
            eprintln!("error: cannot determine executable directory");
            process::exit(1);
        });

        let plist_source = if exe_dir.join("../../install/com.impact-mcp.pull.plist").exists() {
            exe_dir.join("../../install/com.impact-mcp.pull.plist")
        } else if exe_dir.join("../install/com.impact-mcp.pull.plist").exists() {
            exe_dir.join("../install/com.impact-mcp.pull.plist")
        } else {
            let cwd = std::env::current_dir().unwrap_or_else(|e| {
                eprintln!("error: cannot determine current directory: {e}");
                process::exit(1);
            });
            cwd.join("domains/ai/apps/impact_mcp/install/com.impact-mcp.pull.plist")
        };

        if !plist_source.exists() {
            eprintln!("error: cannot find plist template at {}", plist_source.display());
            process::exit(1);
        }

        let plist_content = fs::read_to_string(&plist_source).unwrap_or_else(|e| {
            eprintln!("error: cannot read plist template: {e}");
            process::exit(1);
        });

        // Replace placeholders
        let binary_path = exe_path.display().to_string();
        let home_path = home.display().to_string();
        let plist_content = plist_content
            .replace("BINARY_PATH_PLACEHOLDER", &binary_path)
            .replace("HOME_PLACEHOLDER", &home_path);

        let plist_dest = launch_agents_dir.join("com.impact-mcp.pull.plist");
        match fs::write(&plist_dest, plist_content) {
            Ok(_) => println!("  [ok]   Wrote {}", plist_dest.display()),
            Err(e) => {
                eprintln!("  [err]  Failed to write plist: {e}");
                process::exit(1);
            }
        }

        // Load the agent
        use std::process::Command;
        match Command::new("launchctl")
            .args(["load", plist_dest.to_str().unwrap()])
            .output()
        {
            Ok(output) => {
                if output.status.success() {
                    println!("  [ok]   LaunchAgent loaded");
                } else {
                    eprintln!(
                        "  [warn] LaunchAgent load failed: {}",
                        String::from_utf8_lossy(&output.stderr)
                    );
                }
            }
            Err(e) => {
                eprintln!("  [warn] Failed to load LaunchAgent: {e}");
            }
        }

        println!("\n✓ Automatic hourly pulls configured!");
        println!("\nThe LaunchAgent will run `impact-mcp pull` every hour.");
        println!("Logs: ~/.impact-mcp/pull.log");
        println!("Errors: ~/.impact-mcp/pull.err.log");
        println!("\nTo stop automatic pulls:");
        println!("  launchctl unload ~/Library/LaunchAgents/com.impact-mcp.pull.plist");
    }
}

const SKILLS: &[(&str, &str)] = &[
    (
        "impact-status.md",
        include_str!("../commands/impact-status.md"),
    ),
    (
        "impact-packet.md",
        include_str!("../commands/impact-packet.md"),
    ),
    (
        "impact-gaps.md",
        include_str!("../commands/impact-gaps.md"),
    ),
    (
        "impact-readiness.md",
        include_str!("../commands/impact-readiness.md"),
    ),
    (
        "impact-archetypes.md",
        include_str!("../commands/impact-archetypes.md"),
    ),
    (
        "impact-projects.md",
        include_str!("../commands/impact-projects.md"),
    ),
];

fn install_skills(target_dir: &std::path::Path) {
    if let Err(e) = std::fs::create_dir_all(target_dir) {
        eprintln!("error: cannot create {}: {e}", target_dir.display());
        process::exit(1);
    }

    println!("Installing skills to {}:", target_dir.display());
    for (filename, content) in SKILLS {
        let dst = target_dir.join(filename);
        match std::fs::write(&dst, content) {
            Ok(_) => println!("  [ok]   {} → {}", filename, dst.display()),
            Err(e) => eprintln!("  [err]  {} — {}", filename, e),
        }
    }
}

fn run_setup(claude_skills_dir: Option<PathBuf>, codex_skills_dir: Option<PathBuf>) {
    use std::fs;

    println!("Setting up impact-mcp integration...\n");

    let home = dirs::home_dir().unwrap_or_else(|| {
        eprintln!("error: cannot determine home directory");
        process::exit(1);
    });

    let do_claude = claude_skills_dir.is_some() || codex_skills_dir.is_none();

    if do_claude {
        // 1. Install Claude skills
        let target = claude_skills_dir.unwrap_or_else(|| home.join(".claude/skills"));
        install_skills(&target);

        // 2. Update ~/.claude/settings.json with MCP server config
        println!("\nMCP Server Configuration:");
        let settings_path = home.join(".claude/settings.json");

        let binary_path = std::env::current_exe()
            .unwrap_or_else(|_| PathBuf::from("impact-mcp"))
            .display()
            .to_string();

        let mcp_config = serde_json::json!({
            "command": binary_path,
            "args": ["serve"],
            "env": {}
        });

        let mut settings = if settings_path.exists() {
            let content = fs::read_to_string(&settings_path).unwrap_or_else(|e| {
                eprintln!("warning: cannot read settings.json: {e}");
                String::from("{}")
            });
            serde_json::from_str::<serde_json::Value>(&content).unwrap_or_else(|e| {
                eprintln!("warning: cannot parse settings.json: {e}");
                serde_json::json!({})
            })
        } else {
            serde_json::json!({})
        };

        // Ensure mcpServers object exists
        if !settings.get("mcpServers").is_some() {
            settings["mcpServers"] = serde_json::json!({});
        }

        // Add or update impact-mcp entry
        settings["mcpServers"]["impact-mcp"] = mcp_config;

        match fs::write(&settings_path, serde_json::to_string_pretty(&settings).unwrap()) {
            Ok(_) => println!("  [ok]   Updated {}", settings_path.display()),
            Err(e) => {
                eprintln!("  [err]  Failed to write settings.json: {e}");
                process::exit(1);
            }
        }
    }

    if let Some(target) = codex_skills_dir {
        install_skills(&target);
    }

    println!("\n✓ Setup complete!");
    println!("\nNext steps:");
    if do_claude {
        println!("  1. Restart Claude Code to load the new MCP server");
        println!("  2. Use commands like /impact-status or /impact-packet");
    }
    println!("  3. Add evidence with: impact-mcp evidence add --summary \"...\"");
    println!("  4. Pull from integrations: impact-mcp pull");
    println!("\nFor automatic hourly pulls, run: impact-mcp setup-cron");
}

async fn run_serve(data_dir: &PathBuf) {
    let server = ImpactServer::new(data_dir.clone());
    let transport = rmcp::transport::io::stdio();
    let service = server.serve(transport).await.unwrap_or_else(|e| {
        eprintln!("error: MCP server failed to start: {e}");
        process::exit(1);
    });
    service.waiting().await.unwrap_or_else(|e| {
        eprintln!("error: MCP server error: {e}");
        process::exit(1);
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[test]
    fn install_skills_copies_all_embedded_skills() {
        let dir = TempDir::new().unwrap();
        install_skills(dir.path());

        for (filename, content) in SKILLS {
            let path = dir.path().join(filename);
            assert!(path.exists(), "{} should exist", filename);
            let on_disk = std::fs::read_to_string(path).unwrap();
            assert_eq!(on_disk, *content, "{} content mismatch", filename);
        }
    }
}
