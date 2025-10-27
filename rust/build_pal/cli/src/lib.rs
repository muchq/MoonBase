pub mod config;
pub mod git;
pub mod client;

pub use config::*;
pub use git::*;
pub use client::*;

use build_pal_core::{CLIConfig, ExecutionMode, BuildPalError, Result};
use clap::ArgMatches;

/// Parsed command-line arguments
#[derive(Debug, Clone)]
pub struct CLIArgs {
    pub command: Option<String>,
    pub execution_mode_override: Option<ExecutionMode>,
    pub cancel_build_id: Option<String>,
    pub config_path: Option<String>,
}

impl CLIArgs {
    /// Parse CLI arguments from clap matches
    pub fn from_matches(matches: &ArgMatches) -> Result<Self> {
        // Join multiple command arguments into a single string
        let command = matches.get_many::<String>("command")
            .map(|values| values.map(|s| s.as_str()).collect::<Vec<_>>().join(" "));
        let cancel_build_id = matches.get_one::<String>("cancel").cloned();
        let config_path = matches.get_one::<String>("config").cloned();

        // Validate that we have either a command or cancel request
        if command.is_none() && cancel_build_id.is_none() {
            return Err(BuildPalError::validation("Must provide either a command or --cancel option"));
        }

        // Determine execution mode override
        let execution_mode_override = if matches.get_flag("sync") {
            Some(ExecutionMode::Sync)
        } else if matches.get_flag("async") {
            Some(ExecutionMode::Async)
        } else {
            None
        };

        Ok(Self {
            command,
            execution_mode_override,
            cancel_build_id,
            config_path,
        })
    }
}

/// Main CLI functionality
pub struct BuildPalCLI {
    config: CLIConfig,
}

impl BuildPalCLI {
    pub fn new(config: CLIConfig) -> Self {
        Self { config }
    }

    pub fn config(&self) -> &CLIConfig {
        &self.config
    }

    pub async fn execute_command(&self, command: String) -> Result<()> {
        // Validate command against available commands
        let available_commands = self.config.get_available_commands();
        let command_parts: Vec<&str> = command.split_whitespace().collect();

        if let Some(base_command) = command_parts.first() {
            if !available_commands.contains(&base_command.to_string()) {
                tracing::warn!(
                    "Command '{}' is not in the list of available commands for {}: {:?}",
                    base_command,
                    format!("{:?}", self.config.tool).to_lowercase(),
                    available_commands
                );
            }
        }

        // Prepend the tool name to the command (e.g., "build //..." -> "bazel build //...")
        let tool_name = format!("{:?}", self.config.tool).to_lowercase();
        let full_command = format!("{} {}", tool_name, command);

        tracing::info!(
            "Executing {} command: {} (mode: {:?})",
            tool_name,
            full_command,
            self.config.mode
        );

        // Create build request
        let project_path = std::env::current_dir()
            .map_err(|e| BuildPalError::file_system(format!("Failed to get current directory: {}", e)))?
            .to_string_lossy()
            .to_string();

        let mut build_request = build_pal_core::BuildRequest::new(
            project_path.clone(),
            full_command.clone(),
            self.config.clone(),
            "cli".to_string(),
        );
        
        // Capture git context if available
        if let Ok(git_context) = GitCapture::capture_context(&project_path) {
            build_request = build_request.with_git_context(git_context);
        }
        
        // Create client and submit build
        let client = BuildPalClient::default();
        
        // Check if server is available, wait briefly if not
        if !client.is_server_available().await {
            tracing::info!("Server not available, waiting for server to start...");
            if let Err(_) = client.wait_for_server(std::time::Duration::from_secs(5)).await {
                return Err(BuildPalError::server(
                    "Build Pal server is not running. Please start the server first or ensure it can auto-start."
                ));
            }
        }
        
        // Submit the build request
        match client.submit_build(build_request).await {
            Ok(response) => {
                tracing::info!("Build submitted successfully with ID: {}", response.build_id);
                println!("Build started: {}", response.build_id);
                println!("View logs at: {}", response.web_url);
                
                // In sync mode, we would stream logs here (future enhancement)
                if self.config.mode == build_pal_core::ExecutionMode::Sync {
                    println!("Sync mode streaming not yet implemented. View logs at: {}", response.web_url);
                }
                
                Ok(())
            }
            Err(err) => {
                tracing::error!("Failed to submit build: {}", err);
                Err(BuildPalError::server(format!("Failed to submit build: {}", err)))
            }
        }
    }

    pub async fn cancel_build(&self, build_id: &str) -> Result<()> {
        let build_uuid = build_id.parse::<uuid::Uuid>()
            .map_err(|e| BuildPalError::validation(format!("Invalid build ID format: {}", e)))?;
            
        let client = BuildPalClient::default();
        
        // Check if server is available
        if !client.is_server_available().await {
            return Err(BuildPalError::server("Build Pal server is not running"));
        }
        
        match client.cancel_build(build_uuid).await {
            Ok(()) => {
                println!("Build {} cancelled successfully", build_id);
                Ok(())
            }
            Err(err) => {
                tracing::error!("Failed to cancel build {}: {}", build_id, err);
                Err(BuildPalError::server(format!("Failed to cancel build: {}", err)))
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use build_pal_core::{BuildTool, ExecutionMode, Environment, RetentionPolicy};
    use clap::{Arg, Command};

    fn create_test_config() -> CLIConfig {
        CLIConfig {
            tool: BuildTool::Bazel,
            name: "test-project".to_string(),
            description: None,
            mode: ExecutionMode::Async,
            retention: RetentionPolicy::All,
            retention_duration_days: Some(7),
            environment: Environment::Native,
            parsing: None,
            docker: None,
            ai: None,
        }
    }

    fn create_test_app() -> Command {
        Command::new("build_pal")
            .arg(
                Arg::new("command")
                    .help("Build command to execute")
                    .required(false)
                    .num_args(1..)
                    .trailing_var_arg(true)
                    .allow_hyphen_values(true),
            )
            .arg(
                Arg::new("sync")
                    .long("sync")
                    .help("Run in sync mode")
                    .action(clap::ArgAction::SetTrue)
                    .conflicts_with("async"),
            )
            .arg(
                Arg::new("async")
                    .long("async")
                    .help("Run in async mode")
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
    }

    #[test]
    fn test_cli_creation() {
        let config = create_test_config();
        let cli = BuildPalCLI::new(config.clone());
        assert_eq!(cli.config().name, "test-project");
        assert_eq!(cli.config().tool, BuildTool::Bazel);
    }

    #[test]
    fn test_cli_args_basic_command() {
        let app = create_test_app();
        let matches = app.try_get_matches_from(vec!["build_pal", "build"]).unwrap();
        let args = CLIArgs::from_matches(&matches).unwrap();

        assert_eq!(args.command, Some("build".to_string()));
        assert_eq!(args.execution_mode_override, None);
        assert_eq!(args.cancel_build_id, None);
        assert_eq!(args.config_path, None);
    }

    #[test]
    fn test_cli_args_sync_mode() {
        let app = create_test_app();
        let matches = app.try_get_matches_from(vec!["build_pal", "--sync", "test"]).unwrap();
        let args = CLIArgs::from_matches(&matches).unwrap();

        assert_eq!(args.command, Some("test".to_string()));
        assert_eq!(args.execution_mode_override, Some(ExecutionMode::Sync));
    }

    #[test]
    fn test_cli_args_async_mode() {
        let app = create_test_app();
        let matches = app.try_get_matches_from(vec!["build_pal", "--async", "build"]).unwrap();
        let args = CLIArgs::from_matches(&matches).unwrap();

        assert_eq!(args.command, Some("build".to_string()));
        assert_eq!(args.execution_mode_override, Some(ExecutionMode::Async));
    }

    #[test]
    fn test_cli_args_cancel_build() {
        let app = create_test_app();
        let matches = app.try_get_matches_from(vec!["build_pal", "--cancel", "abc123"]).unwrap();
        let args = CLIArgs::from_matches(&matches).unwrap();

        assert_eq!(args.command, None);
        assert_eq!(args.cancel_build_id, Some("abc123".to_string()));
    }

    #[test]
    fn test_cli_args_custom_config() {
        let app = create_test_app();
        let matches = app.try_get_matches_from(vec!["build_pal", "--config", "/path/to/config", "build"]).unwrap();
        let args = CLIArgs::from_matches(&matches).unwrap();

        assert_eq!(args.command, Some("build".to_string()));
        assert_eq!(args.config_path, Some("/path/to/config".to_string()));
    }

    #[test]
    fn test_cli_args_no_command_or_cancel() {
        let app = create_test_app();
        let matches = app.try_get_matches_from(vec!["build_pal"]).unwrap();
        let result = CLIArgs::from_matches(&matches);

        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("Must provide either a command or --cancel option"));
    }

    #[test]
    fn test_cli_args_conflicting_modes() {
        let app = create_test_app();
        // This should fail at clap level due to conflicts_with
        let result = app.try_get_matches_from(vec!["build_pal", "--sync", "--async", "build"]);
        assert!(result.is_err());
    }

    #[test]
    fn test_cli_args_cancel_with_command_conflict() {
        let app = create_test_app();
        // This should fail at clap level due to conflicts_with_all
        let result = app.try_get_matches_from(vec!["build_pal", "--cancel", "abc123", "build"]);
        assert!(result.is_err());
    }

    #[tokio::test]
    async fn test_execute_command_validation() {
        let config = create_test_config();
        let cli = BuildPalCLI::new(config);

        // Commands should fail because server is not available, but validation logic should work
        let result = cli.execute_command("build //...".to_string()).await;
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("server"));

        // Invalid command should also fail due to server unavailability
        let result = cli.execute_command("invalid-command".to_string()).await;
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("server"));
    }

    #[tokio::test]
    async fn test_execute_command_different_tools() {
        // Test Bazel - should fail due to no server, but tool validation should work
        let mut config = create_test_config();
        config.tool = BuildTool::Bazel;
        let cli = BuildPalCLI::new(config);
        let result = cli.execute_command("build //...".to_string()).await;
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("server"));

        // Test Maven - should fail due to no server, but tool validation should work
        let mut config = create_test_config();
        config.tool = BuildTool::Maven;
        let cli = BuildPalCLI::new(config);
        let result = cli.execute_command("compile".to_string()).await;
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("server"));

        // Test Gradle - should fail due to no server, but tool validation should work
        let mut config = create_test_config();
        config.tool = BuildTool::Gradle;
        let cli = BuildPalCLI::new(config);
        let result = cli.execute_command("build".to_string()).await;
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("server"));
    }

    #[test]
    fn test_git_context_integration() {
        // Test that git context capture is properly integrated into BuildRequest creation
        let config = create_test_config();
        let project_path = "/test/path".to_string();
        let command = "build //...".to_string();
        
        // Create a build request
        let build_request = build_pal_core::BuildRequest::new(
            project_path.clone(),
            command.clone(),
            config.clone(),
            "cli".to_string(),
        );
        
        // Initially should have no git context
        assert!(build_request.git_context.is_none());
        
        // Test adding git context
        let git_context = build_pal_core::GitContext {
            branch: Some("main".to_string()),
            commit_hash: Some("abc123".to_string()),
            commit_message: Some("Test commit".to_string()),
            author: Some("Test User".to_string()),
            has_uncommitted_changes: false,
            diff: None,
        };
        
        let build_request_with_git = build_request.with_git_context(git_context.clone());
        assert!(build_request_with_git.git_context.is_some());
        
        let captured_context = build_request_with_git.git_context.unwrap();
        assert_eq!(captured_context.branch, Some("main".to_string()));
        assert_eq!(captured_context.commit_hash, Some("abc123".to_string()));
        assert_eq!(captured_context.commit_message, Some("Test commit".to_string()));
        assert_eq!(captured_context.author, Some("Test User".to_string()));
        assert!(!captured_context.has_uncommitted_changes);
    }

    #[test]
    fn test_cli_args_complex_command() {
        let app = create_test_app();
        // Test multiple arguments being joined together
        let matches = app.try_get_matches_from(vec!["build_pal", "build", "//...", "--config=release"]).unwrap();
        let args = CLIArgs::from_matches(&matches).unwrap();

        assert_eq!(args.command, Some("build //... --config=release".to_string()));
    }
}