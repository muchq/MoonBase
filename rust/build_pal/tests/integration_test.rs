use build_pal_core::{BuildTool, CLIConfig, ExecutionMode, Environment, RetentionPolicy};
use std::path::Path;

/// Integration test for the build pipeline
/// Tests that all components can work together properly
#[tokio::test]
async fn test_build_pipeline_integration() {
    // Test that we can create and validate a basic configuration
    let config = CLIConfig {
        tool: BuildTool::Bazel,
        name: "test-project".to_string(),
        description: Some("Integration test project".to_string()),
        mode: ExecutionMode::Async,
        retention: RetentionPolicy::All,
        retention_duration_days: Some(7),
        environment: Environment::Native,
        parsing: None,
        docker: None,
        ai: None,
    };

    // Validate configuration
    assert!(config.validate().is_ok());

    // Test available commands
    let commands = config.get_available_commands();
    assert!(!commands.is_empty());
    assert!(commands.contains(&"build".to_string()));
    assert!(commands.contains(&"test".to_string()));

    // Test serialization/deserialization
    let json = serde_json::to_string(&config).expect("Failed to serialize config");
    let deserialized: CLIConfig = serde_json::from_str(&json).expect("Failed to deserialize config");
    
    assert_eq!(config.tool, deserialized.tool);
    assert_eq!(config.name, deserialized.name);
    assert_eq!(config.mode, deserialized.mode);
}

#[test]
fn test_sample_project_config_exists() {
    // Test that our sample project fixture exists
    let config_path = Path::new("rust/build_pal/tests/fixtures/sample_project/.build_pal");
    assert!(config_path.exists(), "Sample project config should exist for integration testing");
}

#[test]
fn test_sample_project_config_valid() {
    // Test that we can read and validate the sample project config
    let config_path = "rust/build_pal/tests/fixtures/sample_project/.build_pal";
    let config_content = std::fs::read_to_string(config_path)
        .expect("Should be able to read sample config");
    
    let config: CLIConfig = serde_json::from_str(&config_content)
        .expect("Sample config should be valid JSON");
    
    assert!(config.validate().is_ok(), "Sample config should be valid");
    assert_eq!(config.tool, BuildTool::Bazel);
    assert_eq!(config.name, "sample-project");
}

#[test]
fn test_build_tool_command_availability() {
    // Test that each build tool has appropriate commands
    let mut config = CLIConfig::default();
    
    // Test Bazel commands
    config.tool = BuildTool::Bazel;
    let bazel_commands = config.get_available_commands();
    assert!(bazel_commands.contains(&"build".to_string()));
    assert!(bazel_commands.contains(&"test".to_string()));
    assert!(bazel_commands.contains(&"query".to_string()));
    
    // Test Maven commands
    config.tool = BuildTool::Maven;
    let maven_commands = config.get_available_commands();
    assert!(maven_commands.contains(&"compile".to_string()));
    assert!(maven_commands.contains(&"package".to_string()));
    assert!(maven_commands.contains(&"test".to_string()));
    
    // Test Gradle commands
    config.tool = BuildTool::Gradle;
    let gradle_commands = config.get_available_commands();
    assert!(gradle_commands.contains(&"build".to_string()));
    assert!(gradle_commands.contains(&"assemble".to_string()));
    assert!(gradle_commands.contains(&"test".to_string()));
}

#[test]
fn test_configuration_validation_edge_cases() {
    let mut config = CLIConfig::default();
    
    // Test empty name validation
    config.name = "".to_string();
    assert!(config.validate().is_err());
    
    // Test zero retention duration
    config.name = "valid-name".to_string();
    config.retention_duration_days = Some(0);
    assert!(config.validate().is_err());
    
    // Test valid configuration
    config.retention_duration_days = Some(30);
    assert!(config.validate().is_ok());
}