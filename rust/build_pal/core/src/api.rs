use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::{Build, CLIConfig, ExecutionMode, GitContext, Project};

/// Request to create a new build
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BuildRequest {
    pub project_path: String,
    pub command: String,
    pub config: CLIConfig,
    pub git_context: Option<GitContext>,
    pub execution_mode: ExecutionMode,
    pub triggered_from: String,
}

/// Response when creating a build
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BuildResponse {
    pub build_id: Uuid,
    pub web_url: String,
    pub status: String,
}

/// Request to cancel a build
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CancelBuildRequest {
    pub build_id: Uuid,
}

/// Response for build status queries
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BuildStatusResponse {
    pub build: Build,
    pub logs_available: bool,
    pub web_url: String,
}

/// Request to get build logs
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LogsRequest {
    pub build_id: Uuid,
    pub start_line: Option<u32>,
    pub end_line: Option<u32>,
}

/// Response containing build logs
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LogsResponse {
    pub build_id: Uuid,
    pub logs: Vec<LogLine>,
    pub total_lines: u32,
    pub has_more: bool,
}

/// Individual log line
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LogLine {
    pub line_number: u32,
    pub timestamp: Option<String>,
    pub content: String,
    pub level: Option<String>,
}

/// Request to list projects
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ListProjectsRequest {
    pub limit: Option<u32>,
    pub offset: Option<u32>,
}

/// Response with project list
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ListProjectsResponse {
    pub projects: Vec<ProjectSummary>,
    pub total: u32,
    pub has_more: bool,
}

/// Project summary for listing
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProjectSummary {
    pub project: Project,
    pub last_build_time: Option<chrono::DateTime<chrono::Utc>>,
    pub last_build_status: Option<String>,
    pub total_builds: u32,
}

/// Request to get project build history
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProjectHistoryRequest {
    pub project_id: Uuid,
    pub limit: Option<u32>,
    pub offset: Option<u32>,
    pub status_filter: Option<String>,
    pub branch_filter: Option<String>,
}

/// Response with project build history
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProjectHistoryResponse {
    pub builds: Vec<Build>,
    pub total: u32,
    pub has_more: bool,
}

/// WebSocket message types
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", content = "data")]
pub enum WebSocketMessage {
    BuildStarted { build_id: Uuid },
    BuildLog { build_id: Uuid, line: LogLine },
    BuildCompleted { build_id: Uuid, exit_code: i32 },
    BuildCancelled { build_id: Uuid },
    BuildError { build_id: Uuid, error: String },
    Subscribe { build_id: Uuid },
    Unsubscribe { build_id: Uuid },
    Ping,
    Pong,
}

/// Health check response
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HealthResponse {
    pub status: String,
    pub version: String,
    pub uptime_seconds: u64,
    pub active_builds: u32,
    pub database_connected: bool,
}

/// Error response
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ErrorResponse {
    pub error: String,
    pub code: String,
    pub details: Option<serde_json::Value>,
}

impl BuildRequest {
    pub fn new(
        project_path: String,
        command: String,
        config: CLIConfig,
        triggered_from: String,
    ) -> Self {
        Self {
            project_path,
            command,
            execution_mode: config.mode.clone(),
            config,
            git_context: None,
            triggered_from,
        }
    }

    pub fn with_git_context(mut self, git_context: GitContext) -> Self {
        self.git_context = Some(git_context);
        self
    }

    pub fn with_execution_mode(mut self, mode: ExecutionMode) -> Self {
        self.execution_mode = mode;
        self
    }
}

impl BuildResponse {
    pub fn new(build_id: Uuid, web_url: String) -> Self {
        Self {
            build_id,
            web_url,
            status: "created".to_string(),
        }
    }
}

impl LogLine {
    pub fn new(line_number: u32, content: String) -> Self {
        Self {
            line_number,
            timestamp: None,
            content,
            level: None,
        }
    }

    pub fn with_timestamp(mut self, timestamp: String) -> Self {
        self.timestamp = Some(timestamp);
        self
    }

    pub fn with_level(mut self, level: String) -> Self {
        self.level = Some(level);
        self
    }
}
