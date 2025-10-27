use build_pal_core::CLIConfig;
use anyhow::{Context, Result};
use std::path::{Path, PathBuf};

/// Configuration file parser
pub struct ConfigParser;

impl ConfigParser {
    /// Read and parse a .build_pal configuration file
    pub fn read_config<P: AsRef<Path>>(path: P) -> Result<CLIConfig> {
        let path_ref = path.as_ref();
        
        // Check if file exists
        if !path_ref.exists() {
            return Err(anyhow::anyhow!(
                "Configuration file not found: {}",
                path_ref.display()
            ));
        }

        // Check if it's actually a file
        if !path_ref.is_file() {
            return Err(anyhow::anyhow!(
                "Configuration path is not a file: {}",
                path_ref.display()
            ));
        }

        // Read file content
        let content = std::fs::read_to_string(path_ref)
            .with_context(|| format!("Failed to read config file: {}", path_ref.display()))?;

        // Check for empty file
        if content.trim().is_empty() {
            return Err(anyhow::anyhow!(
                "Configuration file is empty: {}",
                path_ref.display()
            ));
        }

        // Parse JSON with detailed error information
        let config: CLIConfig = serde_json::from_str(&content)
            .with_context(|| {
                format!(
                    "Failed to parse config file as JSON: {}\nContent preview: {}",
                    path_ref.display(),
                    Self::truncate_content(&content, 100)
                )
            })?;

        // Validate configuration
        config.validate()
            .map_err(|e| anyhow::anyhow!(
                "Config validation failed for {}: {}",
                path_ref.display(),
                e
            ))?;

        tracing::info!(
            "Successfully loaded configuration from {}: {} ({})",
            path_ref.display(),
            config.name,
            format!("{:?}", config.tool).to_lowercase()
        );

        Ok(config)
    }

    /// Find .build_pal config file in current directory or parent directories
    pub fn find_config() -> Result<CLIConfig> {
        let current_dir = std::env::current_dir()
            .with_context(|| "Failed to get current directory")?;

        match Self::find_config_path(&current_dir) {
            Some(config_path) => Self::read_config(config_path),
            None => Err(anyhow::anyhow!(
                "No .build_pal config file found in current directory or parent directories.\n\
                 To get started, create a .build_pal file in your project root with content like:\n\
                 {{\n\
                 \x20\x20\"tool\": \"bazel\",\n\
                 \x20\x20\"name\": \"my-project\",\n\
                 \x20\x20\"mode\": \"async\"\n\
                 }}"
            )),
        }
    }

    /// Find the path to .build_pal config file, returns None if not found
    pub fn find_config_path(start_dir: &Path) -> Option<PathBuf> {
        let mut current_dir = start_dir;

        loop {
            let config_path = current_dir.join(".build_pal");
            
            if config_path.exists() && config_path.is_file() {
                return Some(config_path);
            }

            // Move to parent directory
            match current_dir.parent() {
                Some(parent) => current_dir = parent,
                None => break,
            }
        }

        None
    }

    /// Truncate content for error messages
    fn truncate_content(content: &str, max_len: usize) -> String {
        if content.len() <= max_len {
            content.to_string()
        } else {
            format!("{}...", &content[..max_len])
        }
    }

    /// Validate that required fields are present in a config
    pub fn validate_required_fields(config: &CLIConfig) -> Result<()> {
        if config.name.trim().is_empty() {
            return Err(anyhow::anyhow!("Required field 'name' is missing or empty"));
        }

        // Tool is an enum so it's always valid if deserialization succeeded
        // Mode, retention, environment have defaults so they're always present

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use build_pal_core::{BuildTool, ExecutionMode, Environment, RetentionPolicy};


    #[test]
    fn test_read_config_missing_file() {
        let config_path = "/tmp/nonexistent_build_pal_test/.build_pal";
        
        let result = ConfigParser::read_config(&config_path);
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("Configuration file not found"));
    }

    #[test]
    fn test_find_config_path_not_found() {
        // Use a path that definitely won't have a .build_pal file
        let empty_dir = std::path::Path::new("/usr/bin");
        let found_path = ConfigParser::find_config_path(empty_dir);
        // This should not find a config file in system directories
        assert!(found_path.is_none());
    }

    #[test]
    fn test_config_parsing_logic() {
        // Test the core parsing logic with valid JSON
        let valid_json = r#"{
            "tool": "bazel",
            "name": "test-project",
            "mode": "async",
            "retention": "all",
            "environment": "native"
        }"#;
        
        let config: Result<CLIConfig, _> = serde_json::from_str(valid_json);
        assert!(config.is_ok());
        let config = config.unwrap();
        assert_eq!(config.tool, BuildTool::Bazel);
        assert_eq!(config.name, "test-project");
        assert_eq!(config.mode, ExecutionMode::Async);
    }

    #[test]
    fn test_malformed_json_parsing() {
        // Test malformed JSON
        let malformed_json = r#"{
            "tool": "bazel",
            "name": "test-project"
            "mode": "async"
        }"#; // Missing comma
        
        let result: Result<CLIConfig, _> = serde_json::from_str(malformed_json);
        assert!(result.is_err());
    }

    #[test]
    fn test_invalid_tool_parsing() {
        // Test invalid tool type
        let invalid_tool_json = r#"{
            "tool": "invalid-tool",
            "name": "test-project",
            "mode": "async"
        }"#;
        
        let result: Result<CLIConfig, _> = serde_json::from_str(invalid_tool_json);
        assert!(result.is_err());
    }

    #[test]
    fn test_different_build_tools_parsing() {
        // Test Maven config
        let maven_json = r#"{
            "tool": "maven",
            "name": "maven-project",
            "mode": "sync",
            "retention": "error",
            "retention_duration_days": 30,
            "environment": "native"
        }"#;
        
        let maven_config: CLIConfig = serde_json::from_str(maven_json).unwrap();
        assert_eq!(maven_config.tool, BuildTool::Maven);
        assert_eq!(maven_config.mode, ExecutionMode::Sync);
        assert_eq!(maven_config.retention, RetentionPolicy::Error);
        assert_eq!(maven_config.retention_duration_days, Some(30));

        // Test Gradle config
        let gradle_json = r#"{
            "tool": "gradle",
            "name": "gradle-project",
            "mode": "async",
            "retention": "all",
            "environment": "docker"
        }"#;
        
        let gradle_config: CLIConfig = serde_json::from_str(gradle_json).unwrap();
        assert_eq!(gradle_config.tool, BuildTool::Gradle);
        assert_eq!(gradle_config.environment, Environment::Docker);
    }

    #[test]
    fn test_find_config_with_helpful_error() {
        // This should fail in test environment and provide helpful error message
        let result = ConfigParser::find_config();
        assert!(result.is_err());
        let error_msg = result.unwrap_err().to_string();
        assert!(error_msg.contains("No .build_pal config file found"));
        assert!(error_msg.contains("To get started, create a .build_pal file"));
    }

    #[test]
    fn test_truncate_content() {
        let long_content = "a".repeat(200);
        let truncated = ConfigParser::truncate_content(&long_content, 50);
        assert_eq!(truncated.len(), 53); // 50 + "..."
        assert!(truncated.ends_with("..."));
        
        let short_content = "short";
        let not_truncated = ConfigParser::truncate_content(&short_content, 50);
        assert_eq!(not_truncated, "short");
    }

    #[test]
    fn test_validate_required_fields() {
        let mut config = CLIConfig {
            tool: BuildTool::Bazel,
            name: "valid-name".to_string(),
            description: None,
            mode: ExecutionMode::Async,
            retention: RetentionPolicy::All,
            retention_duration_days: Some(7),
            environment: Environment::Native,
            parsing: None,
            docker: None,
            ai: None,
        };

        // Valid config should pass
        assert!(ConfigParser::validate_required_fields(&config).is_ok());

        // Empty name should fail
        config.name = "".to_string();
        let result = ConfigParser::validate_required_fields(&config);
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("Required field 'name' is missing or empty"));

        // Whitespace-only name should fail
        config.name = "   ".to_string();
        let result = ConfigParser::validate_required_fields(&config);
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("Required field 'name' is missing or empty"));
    }

    #[test]
    fn test_config_validation_edge_cases() {
        // Test empty name validation through parsing
        let empty_name_json = r#"{
            "tool": "bazel",
            "name": "",
            "mode": "async",
            "retention": "all",
            "environment": "native"
        }"#;
        
        let config: CLIConfig = serde_json::from_str(empty_name_json).unwrap();
        let result = config.validate();
        assert!(result.is_err());
        assert!(result.unwrap_err().contains("Project name cannot be empty"));

        // Test zero retention duration
        let zero_retention_json = r#"{
            "tool": "bazel",
            "name": "test-project",
            "mode": "async",
            "retention": "all",
            "retention_duration_days": 0,
            "environment": "native"
        }"#;
        
        let config: CLIConfig = serde_json::from_str(zero_retention_json).unwrap();
        let result = config.validate();
        assert!(result.is_err());
        assert!(result.unwrap_err().contains("Retention duration must be greater than 0 days"));
    }
}