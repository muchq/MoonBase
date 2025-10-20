pub mod api;
pub mod execution;
pub mod plugins;
pub mod storage;

pub use api::*;
pub use execution::*;
pub use plugins::*;
pub use storage::*;

use build_pal_core::{Build, BuildRequest, Project};
use anyhow::Result;
use std::sync::Arc;
use tokio::sync::RwLock;
use uuid::Uuid;

/// Main server application
pub struct BuildPalServer {
    builds: Arc<RwLock<std::collections::HashMap<Uuid, Build>>>,
    projects: Arc<RwLock<std::collections::HashMap<Uuid, Project>>>,
}

impl BuildPalServer {
    pub fn new() -> Self {
        Self {
            builds: Arc::new(RwLock::new(std::collections::HashMap::new())),
            projects: Arc::new(RwLock::new(std::collections::HashMap::new())),
        }
    }

    pub async fn submit_build(&self, request: BuildRequest) -> Result<Uuid> {
        // Placeholder implementation
        let build_id = Uuid::new_v4();
        tracing::info!("Submitted build {} for command: {}", build_id, request.command);
        Ok(build_id)
    }

    pub async fn get_build(&self, build_id: Uuid) -> Option<Build> {
        let builds = self.builds.read().await;
        builds.get(&build_id).cloned()
    }
}

impl Default for BuildPalServer {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use build_pal_core::{CLIConfig, BuildTool, ExecutionMode, Environment, RetentionPolicy};

    #[tokio::test]
    async fn test_server_creation() {
        let server = BuildPalServer::new();
        
        // Test that we can create a build request
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

        let request = BuildRequest::new(
            "/path/to/project".to_string(),
            "bazel build //...".to_string(),
            config,
            "test".to_string(),
        );

        let build_id = server.submit_build(request).await.unwrap();
        assert!(!build_id.is_nil());
    }
}