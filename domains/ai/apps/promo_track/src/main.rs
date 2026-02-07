mod cli;

use std::path::PathBuf;
use std::process;

use clap::Parser;
use tracing_subscriber::EnvFilter;

use promo_track::agent::Agent;
use promo_track::archetype::Archetype;
use promo_track::evidence::{EvidenceCard, EvidenceSource, EvidenceStore};
use promo_track::integrations::Connector;
use promo_track::rubric::{self, Rubric};

use crate::cli::{Cli, Command};

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
        Command::Chat => run_chat(&data_dir).await,
        Command::Status => run_status(&data_dir).await,
        Command::Packet => run_packet(&data_dir).await,
        Command::Readiness => run_readiness(&data_dir).await,
        Command::Archetypes => run_archetypes(),
        Command::Rubric { subcommand } => run_rubric(&data_dir, subcommand),
        Command::Evidence { subcommand } => run_evidence(&data_dir, subcommand),
        Command::Pull => run_pull(&data_dir),
    }
}

fn default_data_dir() -> PathBuf {
    dirs::data_local_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join("stafftrack")
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

fn build_agent(data_dir: &PathBuf) -> Agent {
    let store = EvidenceStore::open(data_dir).unwrap_or_else(|e| {
        eprintln!("error: cannot open evidence store: {e}");
        process::exit(1);
    });
    let rubric = load_rubric(data_dir);
    Agent::with_llm(store, rubric)
}

async fn run_chat(data_dir: &PathBuf) {
    let agent = build_agent(data_dir);

    println!("StaffTrack v{}", env!("CARGO_PKG_VERSION"));
    if let Some(model) = agent.model_name() {
        println!("LLM: {model}");
    } else {
        println!("LLM: disabled (set ANTHROPIC_API_KEY or OPENAI_API_KEY to enable)");
    }
    println!("Type \"help\" for commands, \"quit\" to exit.\n");

    let mut rl = rustyline::DefaultEditor::new().unwrap_or_else(|e| {
        eprintln!("error: cannot initialize readline: {e}");
        process::exit(1);
    });

    loop {
        match rl.readline("stafftrack> ") {
            Ok(line) => {
                let trimmed = line.trim();
                if trimmed.is_empty() {
                    continue;
                }
                if trimmed == "quit" || trimmed == "exit" {
                    break;
                }
                let _ = rl.add_history_entry(trimmed);
                let response = agent.handle(trimmed).await;
                println!("\n{response}");
            }
            Err(
                rustyline::error::ReadlineError::Interrupted
                | rustyline::error::ReadlineError::Eof,
            ) => {
                break;
            }
            Err(e) => {
                eprintln!("error: {e}");
                break;
            }
        }
    }
}

async fn run_status(data_dir: &PathBuf) {
    let agent = build_agent(data_dir);
    let response = agent.handle("draft my weekly status update").await;
    println!("{response}");
}

async fn run_packet(data_dir: &PathBuf) {
    let agent = build_agent(data_dir);
    let response = agent.handle("update my promotion packet").await;
    println!("{response}");
}

async fn run_readiness(data_dir: &PathBuf) {
    let agent = build_agent(data_dir);
    let response = agent.handle("explain my readiness score").await;
    println!("{response}");
}

fn run_archetypes() {
    println!("Staff Archetypes:\n");
    for arch in Archetype::ALL {
        println!("  {:<15} {}", arch.label(), arch.description());
    }
    println!(
        "\nArchetypes are advisory and combinable. Select yours with \
         `stafftrack chat` and ask \"which archetype should I lean into?\""
    );
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
                println!("No evidence cards yet. Use `stafftrack evidence add` or `stafftrack pull`.");
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

fn run_pull(data_dir: &PathBuf) {
    use promo_track::integrations::{gdocs::GdocsConnector, github::GithubConnector, jira::JiraConnector, slack::SlackConnector};

    let connectors: Vec<Box<dyn Connector>> = vec![
        Box::new(GithubConnector::new()),
        Box::new(JiraConnector::new()),
        Box::new(SlackConnector::new()),
        Box::new(GdocsConnector::new()),
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
        match connector.pull() {
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
