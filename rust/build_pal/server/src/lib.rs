pub mod api;
pub mod execution;
pub mod logs;
pub mod plugins;
pub mod storage;

pub use api::*;
pub use execution::*;
pub use logs::*;
pub use plugins::*;
pub use storage::*;

use build_pal_core::{Build, BuildRequest, Project, BuildStatus};
use anyhow::Result;
use std::sync::Arc;
use tokio::sync::RwLock;
use uuid::Uuid;

use std::time::Instant;

/// Main server application
pub struct BuildPalServer {
    builds: Arc<RwLock<std::collections::HashMap<Uuid, Build>>>,
    projects: Arc<RwLock<std::collections::HashMap<Uuid, Project>>>,
    execution_engine: Arc<ExecutionEngine>,
    start_time: Instant,
}

impl BuildPalServer {
    pub fn new() -> Self {
        Self {
            builds: Arc::new(RwLock::new(std::collections::HashMap::new())),
            projects: Arc::new(RwLock::new(std::collections::HashMap::new())),
            execution_engine: Arc::new(ExecutionEngine::new()),
            start_time: Instant::now(),
        }
    }

    pub fn with_execution_engine(execution_engine: Arc<ExecutionEngine>) -> Self {
        Self {
            builds: Arc::new(RwLock::new(std::collections::HashMap::new())),
            projects: Arc::new(RwLock::new(std::collections::HashMap::new())),
            execution_engine,
            start_time: Instant::now(),
        }
    }

    pub async fn submit_build(&self, request: BuildRequest) -> Result<Uuid> {
        // Create a new build from the request
        let project_id = self.get_or_create_project(&request).await?;

        let build = Build::new(
            project_id,
            request.command.clone(),
            request.execution_mode,
            request.config.environment,
            request.triggered_from,
            request.project_path,
        );

        let build_id = build.id;

        // Store the build
        {
            let mut builds = self.builds.write().await;
            builds.insert(build_id, build.clone());
        }

        tracing::info!("Submitted build {} for command: {}", build_id, request.command);

        // Spawn execution in background
        let execution_engine = self.execution_engine.clone();
        let builds = self.builds.clone();
        tokio::spawn(async move {
            tracing::info!("Starting execution for build {}", build_id);

            // Mark build as running
            {
                let mut builds_lock = builds.write().await;
                if let Some(stored_build) = builds_lock.get_mut(&build_id) {
                    stored_build.status = BuildStatus::Running;
                }
            }

            // Execute the build
            let result = execution_engine.execute_build(&build).await;

            // Update build status based on result
            let mut builds_lock = builds.write().await;
            if let Some(stored_build) = builds_lock.get_mut(&build_id) {
                match result {
                    Ok(exec_result) => {
                        stored_build.status = exec_result.status.clone();
                        stored_build.exit_code = Some(exec_result.exit_code);
                        stored_build.end_time = Some(chrono::Utc::now());
                        stored_build.duration_ms = Some(exec_result.duration_ms);
                        tracing::info!(
                            "Build {} completed with status {:?} (exit code: {})",
                            build_id, exec_result.status, exec_result.exit_code
                        );
                    }
                    Err(e) => {
                        stored_build.status = BuildStatus::Failed;
                        stored_build.exit_code = Some(-1);
                        stored_build.end_time = Some(chrono::Utc::now());
                        tracing::error!("Build {} failed with error: {}", build_id, e);
                    }
                }
            }
        });

        Ok(build_id)
    }

    pub async fn get_build(&self, build_id: Uuid) -> Option<Build> {
        let builds = self.builds.read().await;
        builds.get(&build_id).cloned()
    }

    pub async fn get_active_build_count(&self) -> u32 {
        let builds = self.builds.read().await;
        builds.values()
            .filter(|build| matches!(build.status, BuildStatus::Queued | BuildStatus::Running))
            .count() as u32
    }

    pub fn get_uptime_seconds(&self) -> u64 {
        self.start_time.elapsed().as_secs()
    }

    /// Get the execution engine for direct access to build execution and logs
    pub fn get_execution_engine(&self) -> Arc<ExecutionEngine> {
        self.execution_engine.clone()
    }

    /// Get the log manager for direct access to log streaming
    pub fn get_log_manager(&self) -> Arc<LogManager> {
        self.execution_engine.get_log_manager()
    }

    /// Get logs for a specific build (as strings)
    pub async fn get_build_logs(&self, build_id: Uuid) -> Option<Vec<String>> {
        self.execution_engine.get_build_logs(build_id).await
    }

    /// List all projects with their summaries
    pub async fn list_projects(&self) -> Vec<build_pal_core::ProjectSummary> {
        let projects = self.projects.read().await;
        let builds = self.builds.read().await;

        projects
            .values()
            .map(|project| {
                // Find builds for this project
                let project_builds: Vec<_> = builds
                    .values()
                    .filter(|build| build.project_id == project.id)
                    .collect();

                // Get last build info
                let last_build = project_builds
                    .iter()
                    .max_by_key(|build| build.start_time);

                let last_build_time = last_build.map(|b| b.start_time);
                let last_build_status = last_build.map(|b| format!("{:?}", b.status).to_lowercase());
                let total_builds = project_builds.len() as u32;

                build_pal_core::ProjectSummary {
                    project: project.clone(),
                    last_build_time,
                    last_build_status,
                    total_builds,
                }
            })
            .collect()
    }

    /// Get all builds for a specific project
    pub async fn get_project_builds(&self, project_id: Uuid) -> Vec<Build> {
        let builds = self.builds.read().await;

        let mut project_builds: Vec<Build> = builds
            .values()
            .filter(|build| build.project_id == project_id)
            .cloned()
            .collect();

        // Sort by start time, most recent first
        project_builds.sort_by(|a, b| b.start_time.cmp(&a.start_time));

        project_builds
    }

    async fn get_or_create_project(&self, request: &BuildRequest) -> Result<Uuid> {
        let project_name = request.config.name.clone();
        let project_path = request.project_path.clone();
        let tool_type = request.config.tool.clone();

        // Check if project already exists
        {
            let projects = self.projects.read().await;
            for project in projects.values() {
                if project.path == project_path {
                    return Ok(project.id);
                }
            }
        }

        // Create new project
        let project = Project::new(project_name, project_path, tool_type);
        let project_id = project.id;

        {
            let mut projects = self.projects.write().await;
            projects.insert(project_id, project);
        }

        Ok(project_id)
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