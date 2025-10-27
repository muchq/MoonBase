use build_pal_cli::{BuildPalCLI, ConfigParser, CLIArgs};
use build_pal_core::{LoggingConfig, LogLevel, BuildPalError, log_build_pal_error};
use clap::{Arg, Command};

#[tokio::main]
async fn main() -> Result<(), BuildPalError> {
    // Initialize structured logging
    let logging_config = LoggingConfig::for_component("build_pal_cli")
        .with_level(LogLevel::Info);
    
    if let Err(e) = logging_config.init() {
        eprintln!("Failed to initialize logging: {}", e);
        // Continue without structured logging
    }

    let matches = Command::new("build_pal")
        .version("0.1.0")
        .about("Build Pal CLI - Unified build tool interface")
        .arg(
            Arg::new("command")
                .help("Build command to execute (e.g., 'build //...', 'test', 'clean')")
                .required(true)
                .index(1),
        )
        .arg(
            Arg::new("sync")
                .long("sync")
                .help("Run in sync mode (stream output to CLI)")
                .action(clap::ArgAction::SetTrue)
                .conflicts_with("async"),
        )
        .arg(
            Arg::new("async")
                .long("async")
                .help("Run in async mode (background execution)")
                .action(clap::ArgAction::SetTrue)
                .conflicts_with("sync"),
        )
        .arg(
            Arg::new("cancel")
                .short('c')
                .long("cancel")
                .help("Cancel a running build by ID")
                .value_name("BUILD_ID")
                .conflicts_with_all(&["command", "sync", "async"]),
        )
        .arg(
            Arg::new("config")
                .long("config")
                .help("Path to .build_pal config file")
                .value_name("PATH"),
        )
        .get_matches();

    // Parse CLI arguments
    let args = CLIArgs::from_matches(&matches)
        .map_err(|e| BuildPalError::validation(format!("Invalid command line arguments: {}", e)))?;
    
    // Handle cancellation request
    if let Some(build_id) = &args.cancel_build_id {
        // For cancellation, we don't need full config, just create a minimal CLI instance
        let minimal_config = build_pal_core::CLIConfig {
            tool: build_pal_core::BuildTool::Bazel, // Default, not used for cancellation
            name: "temp".to_string(),
            description: None,
            mode: build_pal_core::ExecutionMode::Async,
            retention: build_pal_core::RetentionPolicy::All,
            retention_duration_days: Some(7),
            environment: build_pal_core::Environment::Native,
            parsing: None,
            docker: None,
            ai: None,
        };
        let cli = BuildPalCLI::new(minimal_config);
        match cli.cancel_build(build_id).await {
            Ok(_) => {
                println!("Build {} cancelled successfully", build_id);
                return Ok(());
            }
            Err(e) => {
                log_build_pal_error(&e, Some("build_cancellation"));
                eprintln!("Error: {}", e.user_message());
                return Err(e);
            }
        }
    }

    let command = args.command.as_ref().unwrap();
    
    // Load configuration
    let mut config = if let Some(config_path) = &args.config_path {
        ConfigParser::read_config(config_path)
            .map_err(|e| {
                let err = BuildPalError::from(e);
                log_build_pal_error(&err, Some("config_loading"));
                err
            })?
    } else {
        ConfigParser::find_config()
            .map_err(|e| {
                let err = BuildPalError::from(e);
                log_build_pal_error(&err, Some("config_discovery"));
                err
            })?
    };

    // Apply CLI overrides
    if let Some(mode_override) = args.execution_mode_override {
        config.mode = mode_override;
    }
    
    // Create CLI instance
    let cli = BuildPalCLI::new(config);
    
    // Execute command
    match cli.execute_command(command.clone()).await {
        Ok(_) => {
            println!("Build command '{}' submitted successfully", command);
            Ok(())
        }
        Err(e) => {
            log_build_pal_error(&e, Some("command_execution"));
            eprintln!("Error: {}", e.user_message());
            Err(e)
        }
    }
}