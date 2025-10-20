use build_pal_core::{Build, Environment};
use anyhow::Result;
use tokio::process::Command;

/// Build execution engine
pub struct ExecutionEngine;

impl ExecutionEngine {
    pub fn new() -> Self {
        Self
    }

    /// Execute a build command
    pub async fn execute_build(&self, build: &Build) -> Result<i32> {
        match build.environment {
            Environment::Native => self.execute_native(&build.command).await,
            Environment::Docker => self.execute_docker(&build.command).await,
        }
    }

    async fn execute_native(&self, command: &str) -> Result<i32> {
        let parts: Vec<&str> = command.split_whitespace().collect();
        if parts.is_empty() {
            return Err(anyhow::anyhow!("Empty command"));
        }

        let mut cmd = Command::new(parts[0]);
        if parts.len() > 1 {
            cmd.args(&parts[1..]);
        }

        let output = cmd.output().await?;
        Ok(output.status.code().unwrap_or(-1))
    }

    async fn execute_docker(&self, _command: &str) -> Result<i32> {
        // Placeholder for Docker execution
        tracing::info!("Docker execution not yet implemented");
        Ok(0)
    }
}

impl Default for ExecutionEngine {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use build_pal_core::{ExecutionMode, Environment};
    use uuid::Uuid;

    #[tokio::test]
    async fn test_execution_engine_creation() {
        let engine = ExecutionEngine::new();
        
        // Create a simple test build
        let build = Build::new(
            Uuid::new_v4(),
            "echo hello".to_string(),
            ExecutionMode::Async,
            Environment::Native,
            "test".to_string(),
            "/tmp".to_string(),
        );

        // Test that we can attempt to execute (this will succeed with echo)
        let result = engine.execute_build(&build).await;
        assert!(result.is_ok());
    }
}