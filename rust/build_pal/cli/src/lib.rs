pub mod config;
pub mod git;
pub mod client;

pub use config::*;
pub use git::*;
pub use client::*;

use build_pal_core::CLIConfig;
use anyhow::Result;

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
        // Placeholder implementation
        tracing::info!("Executing command: {}", command);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use build_pal_core::{BuildTool, ExecutionMode, Environment, RetentionPolicy};

    #[test]
    fn test_cli_creation() {
        let config = CLIConfig {
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
        };

        let cli = BuildPalCLI::new(config.clone());
        assert_eq!(cli.config().name, "test-project");
        assert_eq!(cli.config().tool, BuildTool::Bazel);
    }
}