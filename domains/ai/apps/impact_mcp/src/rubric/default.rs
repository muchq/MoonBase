use super::{Rubric, RubricDimension};

/// The built-in default rubric modeled on common Staff-engineer
/// expectations across the industry.
pub fn default_rubric() -> Rubric {
    Rubric {
        name: "Default Impact Rubric".into(),
        version: 2,
        dimensions: vec![
            RubricDimension {
                key: "scope".into(),
                label: "Scope".into(),
                description: "Breadth and ambiguity of problems tackled. Staff engineers \
                              own multi-team or org-level problems end-to-end."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "leverage".into(),
                label: "Leverage".into(),
                description: "Multiplier on team or org output. Includes platforms, \
                              frameworks, tooling, and process improvements that amplify \
                              others' work."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "influence".into(),
                label: "Influence".into(),
                description: "Ability to drive alignment, change minds, and shape \
                              technical direction across organizational boundaries \
                              without relying on authority."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "quality".into(),
                label: "Quality".into(),
                description: "Technical excellence, code review caliber, RFC quality, \
                              and the standard of engineering artifacts produced."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "ops".into(),
                label: "Operations".into(),
                description: "Reliability, on-call diligence, incident response, and \
                              systemic risk reduction. Keeping systems healthy so teams \
                              can move fast safely."
                    .into(),
                weight: 1.0,
            },
            RubricDimension {
                key: "strategy".into(),
                label: "Strategy".into(),
                description: "Long-term thinking, risk identification, and ability to \
                              connect technical decisions to business outcomes."
                    .into(),
                weight: 1.0,
            },
        ],
    }
}
