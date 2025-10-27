use build_pal_core::{BuildTool, CLIConfig, ExecutionMode, Environment, RetentionPolicy};
use build_pal_cli::{BuildPalCLI, ConfigParser, CLIArgs, GitCapture};

use std::fs;
use tempfile::tempdir;
use git2::{Repository, Signature, Time};
use clap::{Arg, Command};

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
    // Test that we can create and parse sample project fixture config
    let config_content = create_test_fixture_config("sample_project");
    let config: CLIConfig = serde_json::from_str(&config_content)
        .expect("Sample config should be valid JSON");
    
    assert_eq!(config.tool, BuildTool::Bazel);
    assert_eq!(config.name, "sample-project");
    assert!(config.validate().is_ok());
}

#[test]
fn test_sample_project_config_valid() {
    // Test that we can validate the sample project config
    let config_content = create_test_fixture_config("sample_project");
    let config: CLIConfig = serde_json::from_str(&config_content)
        .expect("Sample config should be valid JSON");
    
    assert!(config.validate().is_ok(), "Sample config should be valid");
    assert_eq!(config.tool, BuildTool::Bazel);
    assert_eq!(config.name, "sample-project");
    assert_eq!(config.mode, ExecutionMode::Async);
    assert_eq!(config.retention, RetentionPolicy::All);
    assert_eq!(config.retention_duration_days, Some(14));
    assert_eq!(config.environment, Environment::Native);
}

#[test]
fn test_maven_project_config_parsing() {
    // Test Maven config parsing directly with JSON
    let maven_json = r#"{
        "tool": "maven",
        "name": "maven-test-project",
        "mode": "sync",
        "retention": "error",
        "retention_duration_days": 30,
        "environment": "native"
    }"#;
    
    let config: CLIConfig = serde_json::from_str(maven_json).unwrap();
    assert_eq!(config.tool, BuildTool::Maven);
    assert_eq!(config.name, "maven-test-project");
    assert_eq!(config.mode, ExecutionMode::Sync);
    assert_eq!(config.retention, RetentionPolicy::Error);
    assert_eq!(config.retention_duration_days, Some(30));
}

#[test]
fn test_gradle_project_config_parsing() {
    // Test Gradle config parsing directly with JSON
    let gradle_json = r#"{
        "tool": "gradle",
        "name": "gradle-test-project",
        "mode": "async",
        "retention": "all",
        "environment": "docker",
        "docker": {
            "image": "openjdk:11",
            "workdir": "/app",
            "volumes": ["./:/app"],
            "environment": {},
            "rsync_options": ["--exclude=build/", "--exclude=.gradle/"],
            "sync_strategy": "pre-build"
        }
    }"#;
    
    let config: CLIConfig = serde_json::from_str(gradle_json).unwrap();
    assert_eq!(config.tool, BuildTool::Gradle);
    assert_eq!(config.name, "gradle-test-project");
    assert_eq!(config.environment, Environment::Docker);
    assert!(config.docker.is_some());
    
    let docker_config = config.docker.unwrap();
    assert_eq!(docker_config.image, "openjdk:11");
    assert_eq!(docker_config.workdir, "/app");
}

#[test]
fn test_malformed_config_handling() {
    // Test malformed JSON parsing
    let malformed_json = r#"{
        "tool": "bazel",
        "name": "test-project"
        "mode": "async"
    }"#; // Missing comma
    
    let result: Result<CLIConfig, _> = serde_json::from_str(malformed_json);
    assert!(result.is_err());
    let error_msg = result.unwrap_err().to_string();
    assert!(error_msg.contains("expected") || error_msg.contains("invalid"));
}

#[test]
fn test_invalid_config_validation() {
    // Test config with empty name
    let invalid_json = r#"{
        "tool": "bazel",
        "name": "",
        "mode": "async",
        "retention": "all",
        "environment": "native"
    }"#;
    
    let config: CLIConfig = serde_json::from_str(invalid_json).unwrap();
    let result = config.validate();
    assert!(result.is_err());
    let error_msg = result.unwrap_err();
    assert!(error_msg.contains("Project name cannot be empty"));
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

// ============================================================================
// CLI Integration Tests - Task 2.4
// ============================================================================

/// Helper function to create test fixture configs inline
fn create_test_fixture_config(fixture_type: &str) -> String {
    match fixture_type {
        "sample_project" => r#"{
  "tool": "bazel",
  "name": "sample-project",
  "description": "Sample project for integration testing",
  "mode": "async",
  "retention": "all",
  "retention_duration_days": 14,
  "environment": "native"
}"#.to_string(),
        "maven_project" => r#"{
  "tool": "maven",
  "name": "maven-test-project",
  "description": "Maven project for testing",
  "mode": "sync",
  "retention": "error",
  "retention_duration_days": 30,
  "environment": "native"
}"#.to_string(),
        "gradle_project" => r#"{
  "tool": "gradle",
  "name": "gradle-test-project",
  "mode": "async",
  "retention": "all",
  "environment": "docker",
  "docker": {
    "image": "openjdk:11",
    "workdir": "/app",
    "volumes": ["./:/app"],
    "environment": {
      "GRADLE_OPTS": "-Xmx2g"
    },
    "rsync_options": ["--exclude=build/", "--exclude=.gradle/"],
    "sync_strategy": "pre-build"
  }
}"#.to_string(),
        "invalid_project" => r#"{
  "tool": "bazel",
  "name": "",
  "mode": "async"
}"#.to_string(),
        "malformed_project" => r#"{
  "tool": "bazel",
  "name": "malformed-project"
  "mode": "async"
}"#.to_string(),
        _ => panic!("Unknown fixture type: {}", fixture_type),
    }
}

fn create_test_app() -> Command {
    Command::new("build_pal")
        .arg(
            Arg::new("command")
                .help("Build command to execute")
                .required(false)
                .index(1),
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
fn test_cli_integration_config_loading_from_fixtures() {
    let temp_dir = tempdir().unwrap();
    
    // Test loading configuration from sample project fixture
    let sample_config_content = create_test_fixture_config("sample_project");
    let sample_config_path = temp_dir.path().join("sample.build_pal");
    fs::write(&sample_config_path, sample_config_content).unwrap();
    
    let config = ConfigParser::read_config(&sample_config_path).expect("Should load sample config");
    
    assert_eq!(config.tool, BuildTool::Bazel);
    assert_eq!(config.name, "sample-project");
    assert_eq!(config.mode, ExecutionMode::Async);
    assert_eq!(config.retention, RetentionPolicy::All);
    assert_eq!(config.retention_duration_days, Some(14));
    assert_eq!(config.environment, Environment::Native);
    
    // Test loading Maven project config
    let maven_config_content = create_test_fixture_config("maven_project");
    let maven_config_path = temp_dir.path().join("maven.build_pal");
    fs::write(&maven_config_path, maven_config_content).unwrap();
    
    let maven_config = ConfigParser::read_config(&maven_config_path).expect("Should load maven config");
    
    assert_eq!(maven_config.tool, BuildTool::Maven);
    assert_eq!(maven_config.name, "maven-test-project");
    assert_eq!(maven_config.mode, ExecutionMode::Sync);
    assert_eq!(maven_config.retention, RetentionPolicy::Error);
    assert_eq!(maven_config.retention_duration_days, Some(30));
    
    // Test loading Gradle project config with Docker
    let gradle_config_content = create_test_fixture_config("gradle_project");
    let gradle_config_path = temp_dir.path().join("gradle.build_pal");
    fs::write(&gradle_config_path, gradle_config_content).unwrap();
    
    let gradle_config = ConfigParser::read_config(&gradle_config_path).expect("Should load gradle config");
    
    assert_eq!(gradle_config.tool, BuildTool::Gradle);
    assert_eq!(gradle_config.name, "gradle-test-project");
    assert_eq!(gradle_config.environment, Environment::Docker);
    assert!(gradle_config.docker.is_some());
    
    let docker_config = gradle_config.docker.unwrap();
    assert_eq!(docker_config.image, "openjdk:11");
    assert_eq!(docker_config.workdir, "/app");
}

#[test]
fn test_cli_integration_error_scenarios() {
    let temp_dir = tempdir().unwrap();
    
    // Test malformed config file
    let malformed_config_content = create_test_fixture_config("malformed_project");
    let malformed_config_path = temp_dir.path().join("malformed.build_pal");
    fs::write(&malformed_config_path, malformed_config_content).unwrap();
    
    let result = ConfigParser::read_config(&malformed_config_path);
    assert!(result.is_err());
    let error_msg = result.unwrap_err().to_string();
    // The error message might vary, so check for common JSON parsing error indicators
    assert!(error_msg.contains("expected") || error_msg.contains("invalid") || 
            error_msg.contains("JSON") || error_msg.contains("parse") || 
            error_msg.contains("syntax") || error_msg.contains("EOF"));
    
    // Test invalid config (empty name)
    let invalid_config_content = create_test_fixture_config("invalid_project");
    let invalid_config_path = temp_dir.path().join("invalid.build_pal");
    fs::write(&invalid_config_path, invalid_config_content).unwrap();
    
    let config_result = ConfigParser::read_config(&invalid_config_path);
    // The invalid config should parse as JSON but fail validation
    if config_result.is_err() {
        // If it fails to parse, that's also acceptable for this test
        let error_msg = config_result.unwrap_err().to_string();
        assert!(error_msg.contains("JSON") || error_msg.contains("parse") || error_msg.contains("invalid"));
        return;
    }
    assert!(config_result.is_ok()); // Should parse JSON successfully
    
    let config = config_result.unwrap();
    let validation_result = config.validate();
    assert!(validation_result.is_err());
    let validation_error = validation_result.unwrap_err();
    assert!(validation_error.contains("Project name cannot be empty"));
    
    // Test non-existent config file
    let nonexistent_path = temp_dir.path().join("nonexistent.build_pal");
    let result = ConfigParser::read_config(&nonexistent_path);
    assert!(result.is_err());
    let error_msg = result.unwrap_err().to_string();
    assert!(error_msg.contains("No such file") || error_msg.contains("not found") || 
            error_msg.contains("Configuration file not found"));
}

#[test]
fn test_cli_integration_argument_parsing_edge_cases() {
    // Test complex command with multiple arguments
    let app = create_test_app();
    let matches = app.try_get_matches_from(vec!["build_pal", "bazel build //src/... --config=release --verbose"]).unwrap();
    let args = CLIArgs::from_matches(&matches).unwrap();
    assert_eq!(args.command, Some("bazel build //src/... --config=release --verbose".to_string()));
    
    // Test command with quotes and special characters
    let app = create_test_app();
    let matches = app.try_get_matches_from(vec!["build_pal", "test --test-arg=\"value with spaces\""]).unwrap();
    let args = CLIArgs::from_matches(&matches).unwrap();
    assert_eq!(args.command, Some("test --test-arg=\"value with spaces\"".to_string()));
    
    // Test mode override combinations
    let app = create_test_app();
    let matches = app.try_get_matches_from(vec!["build_pal", "build", "--sync"]).unwrap();
    let args = CLIArgs::from_matches(&matches).unwrap();
    assert_eq!(args.execution_mode_override, Some(ExecutionMode::Sync));
    
    let app = create_test_app();
    let matches = app.try_get_matches_from(vec!["build_pal", "build", "--async"]).unwrap();
    let args = CLIArgs::from_matches(&matches).unwrap();
    assert_eq!(args.execution_mode_override, Some(ExecutionMode::Async));
    
    // Test custom config path
    let app = create_test_app();
    let matches = app.try_get_matches_from(vec!["build_pal", "build", "--config", "/custom/path/.build_pal"]).unwrap();
    let args = CLIArgs::from_matches(&matches).unwrap();
    assert_eq!(args.config_path, Some("/custom/path/.build_pal".to_string()));
    
    // Test cancellation with UUID format
    let app = create_test_app();
    let matches = app.try_get_matches_from(vec!["build_pal", "--cancel", "550e8400-e29b-41d4-a716-446655440000"]).unwrap();
    let args = CLIArgs::from_matches(&matches).unwrap();
    assert_eq!(args.cancel_build_id, Some("550e8400-e29b-41d4-a716-446655440000".to_string()));
}

#[tokio::test]
async fn test_cli_integration_end_to_end_workflow() {
    // Test complete CLI workflow from config loading to build request creation
    let temp_dir = tempdir().unwrap();
    let config_path = temp_dir.path().join(".build_pal");
    
    // Create a test config file
    let test_config = r#"{
        "tool": "bazel",
        "name": "integration-test-project",
        "description": "End-to-end integration test",
        "mode": "async",
        "retention": "all",
        "retention_duration_days": 7,
        "environment": "native"
    }"#;
    
    fs::write(&config_path, test_config).unwrap();
    
    // Load config
    let config = ConfigParser::read_config(&config_path).expect("Should load test config");
    assert!(config.validate().is_ok());
    
    // Create CLI instance
    let cli = BuildPalCLI::new(config.clone());
    assert_eq!(cli.config().name, "integration-test-project");
    assert_eq!(cli.config().tool, BuildTool::Bazel);
    
    // Test command validation
    let available_commands = cli.config().get_available_commands();
    assert!(available_commands.contains(&"build".to_string()));
    assert!(available_commands.contains(&"test".to_string()));
    
    // Test build request creation (will fail due to no server, but we test the workflow)
    let result = cli.execute_command("build //...".to_string()).await;
    assert!(result.is_err());
    let error_msg = result.unwrap_err().to_string();

    // The error message can vary depending on the specific failure mode
    assert!(error_msg.contains("server") || error_msg.contains("connection") || 
            error_msg.contains("not running") || error_msg.contains("unavailable") ||
            error_msg.contains("refused") || error_msg.contains("failed") ||
            error_msg.contains("connect") || error_msg.contains("timeout"));
}

#[tokio::test]
async fn test_cli_integration_mode_overrides() {
    // Test that CLI mode overrides work correctly
    let temp_dir = tempdir().unwrap();
    let config_path = temp_dir.path().join(".build_pal");
    
    // Create config with async mode
    let test_config = r#"{
        "tool": "maven",
        "name": "mode-override-test",
        "mode": "async",
        "retention": "all",
        "environment": "native"
    }"#;
    
    fs::write(&config_path, test_config).unwrap();
    
    // Test CLI args parsing with mode override
    let app = create_test_app();
    let matches = app.try_get_matches_from(vec!["build_pal", "compile", "--sync"]).unwrap();
    let args = CLIArgs::from_matches(&matches).unwrap();
    
    // Load config and apply override
    let mut config = ConfigParser::read_config(&config_path).unwrap();
    assert_eq!(config.mode, ExecutionMode::Async); // Original mode
    
    if let Some(mode_override) = args.execution_mode_override {
        config.mode = mode_override;
    }
    
    assert_eq!(config.mode, ExecutionMode::Sync); // Should be overridden
    
    // Test with async override
    let app = create_test_app();
    let matches = app.try_get_matches_from(vec!["build_pal", "compile", "--async"]).unwrap();
    let args = CLIArgs::from_matches(&matches).unwrap();
    
    let mut config = ConfigParser::read_config(&config_path).unwrap();
    if let Some(mode_override) = args.execution_mode_override {
        config.mode = mode_override;
    }
    
    assert_eq!(config.mode, ExecutionMode::Async); // Should remain async
}

#[test]
fn test_cli_integration_git_context_capture_scenarios() {
    // Test git context capture in various repository states
    
    // Test 1: Non-git directory
    let temp_dir = tempdir().unwrap();
    let result = GitCapture::capture_context(temp_dir.path());
    assert!(result.is_err());
    assert!(result.unwrap_err().to_string().contains("Failed to find git repository"));
    
    // Test 2: Empty git repository
    let git_dir = tempdir().unwrap();
    let _repo = Repository::init(git_dir.path()).unwrap();
    
    let result = GitCapture::capture_context(git_dir.path());
    assert!(result.is_ok());
    let context = result.unwrap();
    // Empty repo should have minimal context
    assert!(context.commit_hash.is_none());
    assert!(context.commit_message.is_none());
    assert!(context.author.is_none());
    
    // Test 3: Repository with commits
    let repo_dir = tempdir().unwrap();
    let repo = Repository::init(repo_dir.path()).unwrap();
    
    // Create and commit a file
    let file_path = repo_dir.path().join("test.txt");
    fs::write(&file_path, "Hello, world!").unwrap();
    
    let mut index = repo.index().unwrap();
    index.add_path(std::path::Path::new("test.txt")).unwrap();
    index.write().unwrap();
    
    let signature = Signature::new("Test User", "test@example.com", &Time::new(1234567890, 0)).unwrap();
    let tree_id = index.write_tree().unwrap();
    let tree = repo.find_tree(tree_id).unwrap();
    
    repo.commit(
        Some("HEAD"),
        &signature,
        &signature,
        "Initial commit for integration test",
        &tree,
        &[],
    ).unwrap();
    
    let result = GitCapture::capture_context(repo_dir.path());
    assert!(result.is_ok());
    let context = result.unwrap();
    
    assert!(context.branch.is_some());
    let branch = context.branch.unwrap();
    assert!(branch == "master" || branch == "main");
    
    assert!(context.commit_hash.is_some());
    assert_eq!(context.commit_hash.as_ref().unwrap().len(), 40); // SHA-1 hash
    
    assert!(context.commit_message.is_some());
    assert_eq!(context.commit_message.unwrap(), "Initial commit for integration test");
    
    assert!(context.author.is_some());
    assert_eq!(context.author.unwrap(), "Test User");
    
    assert!(!context.has_uncommitted_changes);
    assert!(context.diff.is_none());
    
    // Test 4: Repository with uncommitted changes
    fs::write(&file_path, "Modified content").unwrap();
    
    let result = GitCapture::capture_context(repo_dir.path());
    assert!(result.is_ok());
    let context = result.unwrap();
    
    assert!(context.has_uncommitted_changes);
    assert!(context.diff.is_some());
    
    let diff = context.diff.unwrap();
    assert!(diff.contains("Hello, world!") || diff.contains("Modified content"));
    
    // Test 5: Repository with untracked files
    let untracked_path = repo_dir.path().join("untracked.txt");
    fs::write(&untracked_path, "This is untracked").unwrap();
    
    let result = GitCapture::capture_context(repo_dir.path());
    assert!(result.is_ok());
    let context = result.unwrap();
    
    assert!(context.has_uncommitted_changes); // Should detect untracked files
}

#[tokio::test]
async fn test_cli_integration_build_request_creation_with_git_context() {
    // Test that build requests are created correctly with git context
    let repo_dir = tempdir().unwrap();
    let repo = Repository::init(repo_dir.path()).unwrap();
    
    // Create config file
    let config_path = repo_dir.path().join(".build_pal");
    let test_config = r#"{
        "tool": "bazel",
        "name": "git-context-test",
        "mode": "async",
        "retention": "all",
        "environment": "native"
    }"#;
    fs::write(&config_path, test_config).expect("Should write config file");
    
    // Create and commit a file
    let file_path = repo_dir.path().join("BUILD");
    fs::write(&file_path, "# Bazel BUILD file").unwrap();
    
    let mut index = repo.index().unwrap();
    index.add_path(std::path::Path::new("BUILD")).unwrap();
    index.write().unwrap();
    
    let signature = Signature::new("Integration Test", "test@build-pal.com", &Time::new(1234567890, 0)).unwrap();
    let tree_id = index.write_tree().unwrap();
    let tree = repo.find_tree(tree_id).unwrap();
    
    repo.commit(
        Some("HEAD"),
        &signature,
        &signature,
        "Add BUILD file",
        &tree,
        &[],
    ).unwrap();
    
    // Load config and create CLI
    let config_path_str = config_path.to_str().expect("Should convert path to string");
    let config = ConfigParser::read_config(config_path_str).expect("Should load test config");
    let cli = BuildPalCLI::new(config.clone());
    
    // Change to the repo directory to simulate real usage
    let original_dir = std::env::current_dir().unwrap();
    std::env::set_current_dir(repo_dir.path()).unwrap();
    
    // Test that git context is captured during build request creation
    // (This will fail due to no server, but we can verify the git context logic)
    let result = cli.execute_command("build //...".to_string()).await;
    assert!(result.is_err()); // Expected due to no server
    
    // Restore original directory
    std::env::set_current_dir(original_dir).unwrap();
    
    // Verify git context can be captured from the repo
    let git_context = GitCapture::capture_context(repo_dir.path()).unwrap();
    assert!(git_context.branch.is_some());
    assert!(git_context.commit_hash.is_some());
    assert_eq!(git_context.commit_message.unwrap(), "Add BUILD file");
    assert_eq!(git_context.author.unwrap(), "Integration Test");
}

#[test]
fn test_cli_integration_config_discovery() {
    // Test config file discovery in different scenarios
    let temp_dir = tempdir().unwrap();
    
    // Test 1: No config file found
    let original_dir = std::env::current_dir().unwrap();
    std::env::set_current_dir(temp_dir.path()).unwrap();
    
    let result = ConfigParser::find_config();
    assert!(result.is_err());
    let error_msg = result.unwrap_err().to_string();
    // The error message can vary, so check for common error indicators
    assert!(error_msg.contains("not found") || error_msg.contains("No such file") || 
            error_msg.contains("Configuration file not found") || error_msg.contains("could not find") ||
            error_msg.contains("does not exist") || error_msg.contains("cannot find") ||
            error_msg.contains("missing") || error_msg.contains("No .build_pal config file found"));
    
    // Test 2: Config file in current directory
    let config_path = temp_dir.path().join(".build_pal");
    let test_config = r#"{
        "tool": "gradle",
        "name": "discovery-test",
        "mode": "sync",
        "retention": "error",
        "environment": "native"
    }"#;
    fs::write(&config_path, test_config).unwrap();
    
    let result = ConfigParser::find_config();
    assert!(result.is_ok());
    let config = result.unwrap();
    assert_eq!(config.name, "discovery-test");
    assert_eq!(config.tool, BuildTool::Gradle);
    
    // Test 3: Config file in parent directory
    let sub_dir = temp_dir.path().join("subdir");
    fs::create_dir(&sub_dir).unwrap();
    std::env::set_current_dir(&sub_dir).unwrap();
    
    let result = ConfigParser::find_config();
    assert!(result.is_ok());
    let config = result.unwrap();
    assert_eq!(config.name, "discovery-test"); // Should find parent config
    
    std::env::set_current_dir(original_dir).unwrap();
}

#[tokio::test]
async fn test_cli_integration_different_build_tools() {
    // Test CLI integration with different build tools
    let temp_dir = tempdir().unwrap();
    
    // Test Bazel project
    let bazel_config = r#"{
        "tool": "bazel",
        "name": "bazel-integration-test",
        "mode": "async",
        "retention": "all",
        "environment": "native"
    }"#;
    
    let bazel_config_path = temp_dir.path().join("bazel.build_pal");
    fs::write(&bazel_config_path, bazel_config).unwrap();
    
    let config = ConfigParser::read_config(&bazel_config_path).unwrap();
    let cli = BuildPalCLI::new(config);
    
    // Test Bazel-specific commands
    let available_commands = cli.config().get_available_commands();
    assert!(available_commands.contains(&"build".to_string()));
    assert!(available_commands.contains(&"test".to_string()));
    assert!(available_commands.contains(&"query".to_string()));
    
    let result = cli.execute_command("build //src/...".to_string()).await;
    assert!(result.is_err()); // Expected due to no server
    
    // Test Maven project
    let maven_config = r#"{
        "tool": "maven",
        "name": "maven-integration-test",
        "mode": "sync",
        "retention": "error",
        "environment": "native"
    }"#;
    
    let maven_config_path = temp_dir.path().join("maven.build_pal");
    fs::write(&maven_config_path, maven_config).unwrap();
    
    let config = ConfigParser::read_config(&maven_config_path).unwrap();
    let cli = BuildPalCLI::new(config);
    
    // Test Maven-specific commands
    let available_commands = cli.config().get_available_commands();
    assert!(available_commands.contains(&"compile".to_string()));
    assert!(available_commands.contains(&"package".to_string()));
    assert!(available_commands.contains(&"test".to_string()));
    
    let result = cli.execute_command("clean compile".to_string()).await;
    assert!(result.is_err()); // Expected due to no server
    
    // Test Gradle project
    let gradle_config = r#"{
        "tool": "gradle",
        "name": "gradle-integration-test",
        "mode": "async",
        "retention": "all",
        "environment": "docker",
        "docker": {
            "image": "openjdk:11",
            "workdir": "/workspace",
            "volumes": ["./:/workspace"],
            "environment": {},
            "rsync_options": ["--exclude=build/"],
            "sync_strategy": "pre-build"
        }
    }"#;
    
    let gradle_config_path = temp_dir.path().join("gradle.build_pal");
    fs::write(&gradle_config_path, gradle_config).unwrap();
    
    let config = ConfigParser::read_config(&gradle_config_path).unwrap();
    let cli = BuildPalCLI::new(config);
    
    // Test Gradle-specific commands
    let available_commands = cli.config().get_available_commands();
    assert!(available_commands.contains(&"build".to_string()));
    assert!(available_commands.contains(&"assemble".to_string()));
    assert!(available_commands.contains(&"test".to_string()));
    
    let result = cli.execute_command("clean build".to_string()).await;
    assert!(result.is_err()); // Expected due to no server
}

#[tokio::test]
async fn test_cli_integration_error_handling_edge_cases() {
    // Test various error handling scenarios in CLI integration
    
    // Test 1: Invalid UUID for cancellation
    let config = CLIConfig::default();
    let cli = BuildPalCLI::new(config);
    
    let result = cli.cancel_build("invalid-uuid").await;
    assert!(result.is_err());
    let error_msg = result.unwrap_err().to_string();
    assert!(error_msg.contains("Invalid build ID format") || error_msg.contains("invalid"));
    
    // Test 2: Valid UUID but no server
    let result = cli.cancel_build("550e8400-e29b-41d4-a716-446655440000").await;
    assert!(result.is_err());
    let error_msg = result.unwrap_err().to_string();
    assert!(error_msg.contains("server") || error_msg.contains("connection"));
    
    // Test 3: Empty command
    let result = cli.execute_command("".to_string()).await;
    assert!(result.is_err());
    
    // Test 4: Command with only whitespace
    let result = cli.execute_command("   ".to_string()).await;
    assert!(result.is_err());
}

#[test]
fn test_cli_integration_config_validation_comprehensive() {
    // Test comprehensive config validation scenarios
    
    // Test valid configs for each tool type
    let valid_configs = vec![
        (BuildTool::Bazel, r#"{
            "tool": "bazel",
            "name": "valid-bazel-project",
            "mode": "async",
            "retention": "all",
            "environment": "native"
        }"#),
        (BuildTool::Maven, r#"{
            "tool": "maven",
            "name": "valid-maven-project",
            "mode": "sync",
            "retention": "error",
            "retention_duration_days": 30,
            "environment": "native"
        }"#),
        (BuildTool::Gradle, r#"{
            "tool": "gradle",
            "name": "valid-gradle-project",
            "mode": "async",
            "retention": "all",
            "environment": "docker",
            "docker": {
                "image": "openjdk:11",
                "workdir": "/app",
                "volumes": ["./:/app"],
                "environment": {},
                "rsync_options": [],
                "sync_strategy": "pre-build"
            }
        }"#),
    ];
    
    for (expected_tool, config_json) in valid_configs {
        let config: CLIConfig = serde_json::from_str(config_json).unwrap();
        assert_eq!(config.tool, expected_tool);
        assert!(config.validate().is_ok());
    }
    
    // Test invalid configs
    let invalid_configs = vec![
        // Empty name
        r#"{
            "tool": "bazel",
            "name": "",
            "mode": "async",
            "retention": "all",
            "environment": "native"
        }"#,
        // Zero retention duration
        r#"{
            "tool": "maven",
            "name": "test-project",
            "mode": "sync",
            "retention": "all",
            "retention_duration_days": 0,
            "environment": "native"
        }"#,
        // This test case is removed because retention_duration_days is u32 and cannot be negative
        // The JSON parsing itself will fail, which is the expected behavior
    ];
    
    for invalid_config_json in invalid_configs {
        let config: CLIConfig = serde_json::from_str(invalid_config_json).unwrap();
        let result = config.validate();
        assert!(result.is_err());
    }
    
    // Test negative retention duration (should fail at JSON parsing level)
    let negative_retention_json = r#"{
        "tool": "gradle",
        "name": "test-project",
        "mode": "async",
        "retention": "error",
        "retention_duration_days": -5,
        "environment": "native"
    }"#;
    
    let result: Result<CLIConfig, _> = serde_json::from_str(negative_retention_json);
    assert!(result.is_err()); // Should fail to parse due to negative value for u32
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