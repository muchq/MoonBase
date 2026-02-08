use std::collections::HashMap;

use serde::{Deserialize, Serialize};

use crate::archetype::Archetype;
use crate::evidence::EvidenceStore;
use crate::rubric::Rubric;

/// Coverage level for a single rubric dimension.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum CoverageLevel {
    /// No meaningful evidence.
    None,
    /// Sparse or low-confidence evidence.
    Weak,
    /// Moderate evidence, gaps remain.
    Moderate,
    /// Strong, well-supported evidence.
    Strong,
}

impl CoverageLevel {
    fn from_count(n: usize) -> Self {
        match n {
            0 => Self::None,
            1..=2 => Self::Weak,
            3..=5 => Self::Moderate,
            _ => Self::Strong,
        }
    }
}

impl std::fmt::Display for CoverageLevel {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::None => write!(f, "None"),
            Self::Weak => write!(f, "Weak"),
            Self::Moderate => write!(f, "Moderate"),
            Self::Strong => write!(f, "Strong"),
        }
    }
}

/// An identified gap in readiness.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Gap {
    pub dimension: String,
    pub coverage: CoverageLevel,
    pub suggestion: String,
}

/// Full readiness assessment against the active rubric.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReadinessMap {
    /// Rubric dimension key -> coverage level.
    pub dimension_coverage: HashMap<String, CoverageLevel>,
    /// Archetype -> strength score (0.0-1.0).
    pub archetype_strength: HashMap<String, f64>,
    /// Identified gaps ordered by priority.
    pub gaps: Vec<Gap>,
    /// Aggregate readiness score (0.0-1.0).
    pub overall_score: f64,
}

impl ReadinessMap {
    /// Compute readiness from the current evidence store and active rubric.
    pub fn compute(store: &EvidenceStore, rubric: &Rubric) -> Self {
        let mut dimension_coverage = HashMap::new();
        let mut weighted_sum = 0.0;
        let total_weight = rubric.total_weight();

        for dim in &rubric.dimensions {
            let evidence = store.by_rubric_tag(&dim.key);
            let level = CoverageLevel::from_count(evidence.len());
            dimension_coverage.insert(dim.key.clone(), level);

            let score = match level {
                CoverageLevel::None => 0.0,
                CoverageLevel::Weak => 0.25,
                CoverageLevel::Moderate => 0.6,
                CoverageLevel::Strong => 1.0,
            };
            weighted_sum += score * dim.weight;
        }

        let overall_score = if total_weight > 0.0 {
            weighted_sum / total_weight
        } else {
            0.0
        };

        let mut archetype_strength = HashMap::new();
        let total_cards = store.count().max(1) as f64;
        for arch in Archetype::ALL {
            let count = store.by_archetype(arch).len() as f64;
            archetype_strength.insert(arch.label().to_string(), count / total_cards);
        }

        let gaps = rubric
            .dimensions
            .iter()
            .filter_map(|dim| {
                let level = dimension_coverage
                    .get(&dim.key)
                    .copied()
                    .unwrap_or(CoverageLevel::None);
                match level {
                    CoverageLevel::Strong => None,
                    _ => Some(Gap {
                        dimension: dim.label.clone(),
                        coverage: level,
                        suggestion: format!(
                            "Gather more evidence demonstrating {} — {}",
                            dim.label, dim.description
                        ),
                    }),
                }
            })
            .collect();

        Self {
            dimension_coverage,
            archetype_strength,
            gaps,
            overall_score,
        }
    }
}
