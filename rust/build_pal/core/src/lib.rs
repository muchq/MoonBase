pub mod api;
pub mod config;
pub mod types;

pub use api::*;
pub use config::*;
pub use types::*;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_build_creation() {
        let build = Build::new(
            uuid::Uuid::new_v4(),
            "bazel build //...".to_string(),
            ExecutionMode::Async,
            Environment::Native,
            "cli".to_string(),
            "/path/to/project".to_string(),
        );

        assert_eq!(build.status, BuildStatus::Queued);
        assert_eq!(build.execution_mode, ExecutionMode::Async);
        assert_eq!(build.environment, Environment::Native);
        assert_eq!(build.command, "bazel build //...");
        assert!(build.duration().is_some());
        assert!(!build.is_finished());
    }

    #[test]
    fn test_project_creation() {
        let project = Project::new(
            "test-project".to_string(),
            "/path/to/project".to_string(),
            BuildTool::Bazel,
        );

        assert_eq!(project.name, "test-project");
        assert_eq!(project.path, "/path/to/project");
        assert_eq!(project.tool_type, BuildTool::Bazel);
    }

    #[test]
    fn test_cli_config_default() {
        let config = CLIConfig::default();

        assert_eq!(config.tool, BuildTool::Bazel);
        assert_eq!(config.name, "unnamed-project");
        assert_eq!(config.mode, ExecutionMode::Async);
        assert_eq!(config.retention, RetentionPolicy::All);
        assert_eq!(config.retention_duration_days, Some(7));
        assert_eq!(config.environment, Environment::Native);
    }

    #[test]
    fn test_cli_config_validation() {
        let mut config = CLIConfig::default();
        assert!(config.validate().is_ok());

        // Test empty name
        config.name = "".to_string();
        assert!(config.validate().is_err());
        assert!(
            config
                .validate()
                .unwrap_err()
                .contains("Project name cannot be empty")
        );

        // Test zero retention duration
        config.name = "test".to_string();
        config.retention_duration_days = Some(0);
        assert!(config.validate().is_err());
        assert!(
            config
                .validate()
                .unwrap_err()
                .contains("Retention duration must be greater than 0 days")
        );

        // Test valid config with custom duration
        config.retention_duration_days = Some(30);
        assert!(config.validate().is_ok());
    }

    #[test]
    fn test_available_commands() {
        let mut config = CLIConfig::default();

        config.tool = BuildTool::Bazel;
        let bazel_commands = config.get_available_commands();
        assert!(bazel_commands.contains(&"build".to_string()));
        assert!(bazel_commands.contains(&"test".to_string()));

        config.tool = BuildTool::Maven;
        let maven_commands = config.get_available_commands();
        assert!(maven_commands.contains(&"compile".to_string()));
        assert!(maven_commands.contains(&"package".to_string()));

        config.tool = BuildTool::Gradle;
        let gradle_commands = config.get_available_commands();
        assert!(gradle_commands.contains(&"build".to_string()));
        assert!(gradle_commands.contains(&"assemble".to_string()));
    }

    #[test]
    fn test_build_request_creation() {
        let config = CLIConfig::default();
        let request = BuildRequest::new(
            "/path/to/project".to_string(),
            "bazel build //...".to_string(),
            config.clone(),
            "cli".to_string(),
        );

        assert_eq!(request.project_path, "/path/to/project");
        assert_eq!(request.command, "bazel build //...");
        assert_eq!(request.execution_mode, config.mode);
        assert_eq!(request.triggered_from, "cli");
        assert!(request.git_context.is_none());
    }

    #[test]
    fn test_git_context_default() {
        let git_context = GitContext::default();

        assert!(git_context.branch.is_none());
        assert!(git_context.commit_hash.is_none());
        assert!(!git_context.has_uncommitted_changes);
        assert!(git_context.diff.is_none());
    }

    #[test]
    fn test_serialization() {
        let config = CLIConfig::default();
        let json = serde_json::to_string(&config).unwrap();
        let deserialized: CLIConfig = serde_json::from_str(&json).unwrap();

        assert_eq!(config.tool, deserialized.tool);
        assert_eq!(config.name, deserialized.name);
        assert_eq!(
            config.retention_duration_days,
            deserialized.retention_duration_days
        );
    }
}
