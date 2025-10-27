use serde::{Deserialize, Serialize};
use std::collections::HashMap;

use crate::{BuildTool, Environment, ExecutionMode, RetentionPolicy};

/// Main configuration for a build_pal project
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CLIConfig {
    pub tool: BuildTool,
    pub name: String,
    pub description: Option<String>,
    pub mode: ExecutionMode,
    pub retention: RetentionPolicy,
    pub retention_duration_days: Option<u32>,
    pub environment: Environment,
    pub parsing: Option<PluginConfig>,
    pub docker: Option<DockerConfig>,
    pub ai: Option<AIConfig>,
}

/// Plugin configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PluginConfig {
    pub plugin: String,
    pub config: Option<HashMap<String, serde_json::Value>>,
}

/// Docker execution configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DockerConfig {
    pub image: String,
    pub workdir: String,
    pub volumes: Vec<String>,
    pub environment: HashMap<String, String>,
    pub rsync_options: Vec<String>,
    pub sync_strategy: SyncStrategy,
}

/// File synchronization strategy for Docker
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum SyncStrategy {
    PreBuild,
    Continuous,
    PostBuild,
}

/// AI analysis configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AIConfig {
    pub enabled: bool,
    pub provider: String,
    pub api_key: String,
    pub model: String,
    pub max_tokens: Option<u32>,
}

impl Default for CLIConfig {
    fn default() -> Self {
        Self {
            tool: BuildTool::Bazel,
            name: "unnamed-project".to_string(),
            description: None,
            mode: ExecutionMode::Async,
            retention: RetentionPolicy::All,
            retention_duration_days: Some(7), // Default to 1 week
            environment: Environment::Native,
            parsing: None,
            docker: None,
            ai: None,
        }
    }
}

impl Default for DockerConfig {
    fn default() -> Self {
        Self {
            image: "ubuntu:22.04".to_string(),
            workdir: "/workspace".to_string(),
            volumes: vec!["./:/workspace".to_string()],
            environment: HashMap::new(),
            rsync_options: vec![
                "--exclude=node_modules".to_string(),
                "--exclude=.git".to_string(),
                "--exclude=target/".to_string(),
                "--exclude=build/".to_string(),
                "--compress-level=6".to_string(),
                "--preserve-perms".to_string(),
            ],
            sync_strategy: SyncStrategy::PreBuild,
        }
    }
}

impl CLIConfig {
    /// Validate the configuration
    pub fn validate(&self) -> Result<(), String> {
        if self.name.is_empty() {
            return Err("Project name cannot be empty".to_string());
        }

        if let Some(duration) = self.retention_duration_days {
            if duration == 0 {
                return Err("Retention duration must be greater than 0 days".to_string());
            }
        }

        if let Some(docker_config) = &self.docker {
            if docker_config.image.is_empty() {
                return Err("Docker image cannot be empty".to_string());
            }
            if docker_config.workdir.is_empty() {
                return Err("Docker workdir cannot be empty".to_string());
            }
        }

        if let Some(ai_config) = &self.ai {
            if ai_config.enabled && ai_config.api_key.is_empty() {
                return Err("AI API key cannot be empty when AI is enabled".to_string());
            }
        }

        Ok(())
    }

    /// Get available commands for the configured build tool
    pub fn get_available_commands(&self) -> Vec<String> {
        match self.tool {
            BuildTool::Bazel => vec![
                "build".to_string(),
                "test".to_string(),
                "run".to_string(),
                "clean".to_string(),
                "query".to_string(),
            ],
            BuildTool::Maven => vec![
                "compile".to_string(),
                "test".to_string(),
                "package".to_string(),
                "install".to_string(),
                "clean".to_string(),
                "verify".to_string(),
            ],
            BuildTool::Gradle => vec![
                "build".to_string(),
                "test".to_string(),
                "assemble".to_string(),
                "clean".to_string(),
                "check".to_string(),
                "run".to_string(),
            ],
        }
    }
}
