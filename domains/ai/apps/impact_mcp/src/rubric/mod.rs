mod default;

pub use default::default_rubric;

use std::fs;
use std::path::Path;

use serde::{Deserialize, Serialize};

/// A single dimension on which Staff readiness is assessed.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RubricDimension {
    /// Machine-readable key, e.g. "scope".
    pub key: String,
    /// Human-readable label, e.g. "Scope".
    pub label: String,
    /// Long-form description of what this dimension means.
    pub description: String,
    /// Relative weight when computing readiness (default 1.0).
    pub weight: f64,
}

/// A complete rubric — a named, versioned collection of dimensions.
///
/// Rubrics are stored as YAML files and are fully user-overridable.
/// Multiple rubrics can coexist (e.g. company rubric vs. manager rubric).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Rubric {
    pub name: String,
    pub version: u32,
    pub dimensions: Vec<RubricDimension>,
}

impl Rubric {
    pub fn dimension_keys(&self) -> Vec<&str> {
        self.dimensions.iter().map(|d| d.key.as_str()).collect()
    }

    pub fn find_dimension(&self, key: &str) -> Option<&RubricDimension> {
        self.dimensions.iter().find(|d| d.key == key)
    }

    pub fn total_weight(&self) -> f64 {
        self.dimensions.iter().map(|d| d.weight).sum()
    }

    /// Load a rubric from a YAML file.
    pub fn load(path: &Path) -> Result<Self, RubricError> {
        let data = fs::read_to_string(path).map_err(RubricError::Io)?;
        serde_yaml_ng::from_str(&data).map_err(RubricError::Yaml)
    }

    /// Save this rubric to a YAML file.
    pub fn save(&self, path: &Path) -> Result<(), RubricError> {
        let data = serde_yaml_ng::to_string(self).map_err(RubricError::Yaml)?;
        fs::write(path, data).map_err(RubricError::Io)?;
        Ok(())
    }
}

#[derive(Debug)]
pub enum RubricError {
    Io(std::io::Error),
    Yaml(serde_yaml_ng::Error),
}

impl std::fmt::Display for RubricError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(e) => write!(f, "rubric I/O error: {e}"),
            Self::Yaml(e) => write!(f, "rubric YAML error: {e}"),
        }
    }
}

impl std::error::Error for RubricError {}
