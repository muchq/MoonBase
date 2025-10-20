use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

/// Core build execution status
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum BuildStatus {
    Queued,
    Running,
    Completed,
    Failed,
    Cancelled,
}

/// Build execution mode
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum ExecutionMode {
    Sync,
    Async,
}

/// Build execution environment
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum Environment {
    Native,
    Docker,
}

/// Log retention policy
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum RetentionPolicy {
    All,
    Error,
}

/// Build tool type
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, Hash)]
#[serde(rename_all = "lowercase")]
pub enum BuildTool {
    Bazel,
    Maven,
    Gradle,
}

/// Git context information
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GitContext {
    pub branch: Option<String>,
    pub commit_hash: Option<String>,
    pub commit_message: Option<String>,
    pub author: Option<String>,
    pub has_uncommitted_changes: bool,
    pub diff: Option<String>,
}

/// Project information
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Project {
    pub id: Uuid,
    pub name: String,
    pub path: String,
    pub tool_type: BuildTool,
    pub description: Option<String>,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
}

/// Build execution information
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Build {
    pub id: Uuid,
    pub project_id: Uuid,
    pub command: String,
    pub status: BuildStatus,
    pub execution_mode: ExecutionMode,
    pub environment: Environment,
    pub triggered_from: String, // "cli" or "web"
    pub start_time: DateTime<Utc>,
    pub end_time: Option<DateTime<Utc>>,
    pub duration_ms: Option<u64>,
    pub exit_code: Option<i32>,
    pub git_context: Option<GitContext>,
    pub working_directory: String,
    pub logs_stored: bool,
}

/// Build summary with parsed information
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BuildSummary {
    pub build: Build,
    pub error_count: u32,
    pub warning_count: u32,
    pub test_results: Option<TestResults>,
    pub parsed_errors: Vec<ParsedError>,
}

/// Test execution results
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TestResults {
    pub total: u32,
    pub passed: u32,
    pub failed: u32,
    pub skipped: u32,
    pub failed_tests: Vec<FailedTest>,
}

/// Individual test failure
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FailedTest {
    pub name: String,
    pub class: Option<String>,
    pub message: String,
    pub stack_trace: Option<String>,
}

/// Parsed error from build logs
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ParsedError {
    pub file: Option<String>,
    pub line: Option<u32>,
    pub column: Option<u32>,
    pub message: String,
    pub error_type: String,
    pub severity: ErrorSeverity,
}

/// Error severity level
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum ErrorSeverity {
    Error,
    Warning,
    Info,
}

/// AI analysis result
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AIAnalysis {
    pub id: Uuid,
    pub build_id: Uuid,
    pub provider: String,
    pub model: String,
    pub summary: String,
    pub suggested_fixes: Vec<SuggestedFix>,
    pub confidence: f32,
    pub analysis_time_ms: u64,
    pub created_at: DateTime<Utc>,
}

/// AI-suggested fix
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SuggestedFix {
    pub title: String,
    pub description: String,
    pub confidence: f32,
    pub fix_type: String,
    pub code_changes: Option<String>,
}

impl Default for GitContext {
    fn default() -> Self {
        Self {
            branch: None,
            commit_hash: None,
            commit_message: None,
            author: None,
            has_uncommitted_changes: false,
            diff: None,
        }
    }
}

impl Build {
    pub fn new(
        project_id: Uuid,
        command: String,
        execution_mode: ExecutionMode,
        environment: Environment,
        triggered_from: String,
        working_directory: String,
    ) -> Self {
        Self {
            id: Uuid::new_v4(),
            project_id,
            command,
            status: BuildStatus::Queued,
            execution_mode,
            environment,
            triggered_from,
            start_time: Utc::now(),
            end_time: None,
            duration_ms: None,
            exit_code: None,
            git_context: None,
            working_directory,
            logs_stored: true,
        }
    }

    pub fn duration(&self) -> Option<u64> {
        if let Some(end_time) = self.end_time {
            Some((end_time - self.start_time).num_milliseconds() as u64)
        } else {
            Some((Utc::now() - self.start_time).num_milliseconds() as u64)
        }
    }

    pub fn is_finished(&self) -> bool {
        matches!(
            self.status,
            BuildStatus::Completed | BuildStatus::Failed | BuildStatus::Cancelled
        )
    }
}

impl Project {
    pub fn new(name: String, path: String, tool_type: BuildTool) -> Self {
        let now = Utc::now();
        Self {
            id: Uuid::new_v4(),
            name,
            path,
            tool_type,
            description: None,
            created_at: now,
            updated_at: now,
        }
    }
}
