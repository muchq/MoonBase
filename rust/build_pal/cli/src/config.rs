use build_pal_core::CLIConfig;
use anyhow::{Context, Result};
use std::path::Path;

/// Configuration file parser
pub struct ConfigParser;

impl ConfigParser {
    /// Read and parse a .build_pal configuration file
    pub fn read_config<P: AsRef<Path>>(path: P) -> Result<CLIConfig> {
        let content = std::fs::read_to_string(path.as_ref())
            .with_context(|| format!("Failed to read config file: {}", path.as_ref().display()))?;
        
        let config: CLIConfig = serde_json::from_str(&content)
            .with_context(|| "Failed to parse config file as JSON")?;
        
        config.validate()
            .map_err(|e| anyhow::anyhow!("Config validation failed: {}", e))?;
        
        Ok(config)
    }

    /// Find .build_pal config file in current directory or parent directories
    pub fn find_config() -> Result<CLIConfig> {
        let current_dir = std::env::current_dir()
            .with_context(|| "Failed to get current directory")?;
        
        Self::find_config_in_dir(&current_dir)
    }

    fn find_config_in_dir(dir: &Path) -> Result<CLIConfig> {
        let config_path = dir.join(".build_pal");
        
        if config_path.exists() {
            return Self::read_config(config_path);
        }
        
        if let Some(parent) = dir.parent() {
            return Self::find_config_in_dir(parent);
        }
        
        Err(anyhow::anyhow!("No .build_pal config file found in current directory or parent directories"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    // use build_pal_core::{BuildTool, ExecutionMode};
    // use std::fs;
    // use tempfile::tempdir;

    #[test]
    fn test_config_parser_functionality() {
        // Test that ConfigParser has the expected methods
        assert!(ConfigParser::find_config().is_err()); // Should fail in test environment
    }
}