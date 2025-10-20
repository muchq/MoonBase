use build_pal_cli::{BuildPalCLI, ConfigParser};
use clap::{Arg, Command};
use anyhow::Result;

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt::init();

    let matches = Command::new("build_pal")
        .version("0.1.0")
        .about("Build Pal CLI - Unified build tool interface")
        .arg(
            Arg::new("command")
                .help("Build command to execute")
                .required(true)
                .index(1),
        )
        .arg(
            Arg::new("sync")
                .long("sync")
                .help("Run in sync mode")
                .action(clap::ArgAction::SetTrue),
        )
        .arg(
            Arg::new("async")
                .long("async")
                .help("Run in async mode")
                .action(clap::ArgAction::SetTrue),
        )
        .get_matches();

    let command = matches.get_one::<String>("command").unwrap();
    
    // Load configuration
    let config = ConfigParser::find_config()?;
    
    // Create CLI instance
    let cli = BuildPalCLI::new(config);
    
    // Execute command
    cli.execute_command(command.clone()).await?;
    
    println!("Build command '{}' submitted successfully", command);
    
    Ok(())
}