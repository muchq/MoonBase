use serde::{Deserialize, Serialize};

/// Staff archetypes describe *how* impact is created.
///
/// Archetypes are advisory, combinable, and evidence-driven. They
/// influence how evidence is interpreted, which suggestions are
/// prioritized, and how promotion narratives are framed.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Archetype {
    /// Delivery, outcomes, execution across teams.
    TechLead,
    /// Technical direction, long-term strategy.
    Architect,
    /// Ambiguity, novel or high-risk challenges.
    ProblemSolver,
    /// Reliability, quality, and systemic risk reduction.
    Operator,
    /// Multiplying others, raising the organizational bar.
    Mentor,
    /// Extends executive attention across complex organizations.
    RightHand,
    /// Essential cross-team work that holds organizations together.
    Glue,
}

impl Archetype {
    pub const ALL: [Archetype; 7] = [
        Self::TechLead,
        Self::Architect,
        Self::ProblemSolver,
        Self::Operator,
        Self::Mentor,
        Self::RightHand,
        Self::Glue,
    ];

    pub fn label(self) -> &'static str {
        match self {
            Self::TechLead => "Tech Lead",
            Self::Architect => "Architect",
            Self::ProblemSolver => "Problem Solver",
            Self::Operator => "Operator",
            Self::Mentor => "Mentor",
            Self::RightHand => "Right Hand",
            Self::Glue => "Glue",
        }
    }

    pub fn description(self) -> &'static str {
        match self {
            Self::TechLead => {
                "Delivery-oriented leader who drives outcomes and execution across teams"
            }
            Self::Architect => {
                "Sets technical direction and long-term strategy for systems and platforms"
            }
            Self::ProblemSolver => {
                "Tackles ambiguous, novel, or high-risk challenges that others cannot"
            }
            Self::Operator => {
                "Ensures reliability, quality, and systemic risk reduction at scale"
            }
            Self::Mentor => {
                "Multiplies others' effectiveness and raises the organizational engineering bar"
            }
            Self::RightHand => {
                "Extends an executive's attention, borrowing their scope and authority to operate complex organizations"
            }
            Self::Glue => {
                "Does the essential cross-team work that holds organizations together — onboarding, unblocking, filling gaps, and driving alignment"
            }
        }
    }
}

impl std::fmt::Display for Archetype {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.label())
    }
}

/// User's archetype preferences — selected explicitly or inferred.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ArchetypeProfile {
    /// Archetypes the user has explicitly selected.
    pub selected: Vec<Archetype>,
    /// Strengths inferred from the evidence store (archetype -> score 0.0-1.0).
    pub inferred: Vec<(Archetype, f64)>,
}

impl Default for ArchetypeProfile {
    fn default() -> Self {
        Self {
            selected: Vec::new(),
            inferred: Vec::new(),
        }
    }
}

impl ArchetypeProfile {
    /// The dominant archetype: explicitly selected first, otherwise highest inferred.
    pub fn dominant(&self) -> Option<Archetype> {
        if let Some(&first) = self.selected.first() {
            return Some(first);
        }
        self.inferred
            .iter()
            .max_by(|a, b| a.1.partial_cmp(&b.1).unwrap_or(std::cmp::Ordering::Equal))
            .map(|(a, _)| *a)
    }
}
