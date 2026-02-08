use crate::archetype::Archetype;
use crate::output::{draft_status_update, generate_packet_section};

use super::Agent;

/// Handles recognized chat commands from the interactive session.
///
/// Commands are matched case-insensitively against known patterns.
/// Returns `None` for unrecognized input so the caller can fall
/// through to the LLM.
pub struct CommandHandler;

impl CommandHandler {
    /// Try to handle `input` as a known command. Returns `None` if
    /// no command matched (caller should forward to the LLM).
    pub fn try_dispatch(&self, input: &str, agent: &Agent) -> Option<String> {
        let normalized = input.trim().to_lowercase();

        if normalized.contains("what rubric") && normalized.contains("using") {
            return Some(self.show_rubric(agent));
        }
        if normalized.contains("edit") && normalized.contains("rubric") {
            return Some(self.edit_rubric_hint(agent));
        }
        if normalized.contains("archetype") && normalized.contains("showing") {
            return Some(self.show_archetypes(agent));
        }
        if normalized.contains("archetype") && normalized.contains("lean into") {
            return Some(self.suggest_archetype(agent));
        }
        if normalized.contains("missing") || (normalized.contains("improve") && normalized.contains("impact")) {
            return Some(self.show_gaps(agent));
        }
        if normalized.contains("promotion packet") || normalized.contains("impact narrative") || normalized.contains("update my") {
            return Some(self.update_packet(agent));
        }
        if normalized.contains("weekly status") || normalized.contains("status update") {
            return Some(self.draft_status(agent));
        }
        if normalized.contains("readiness score") || normalized.contains("readiness") {
            return Some(self.explain_readiness(agent));
        }
        if normalized == "help" || normalized == "?" {
            return Some(self.help());
        }

        None
    }

    fn show_rubric(&self, agent: &Agent) -> String {
        let r = &agent.rubric;
        let mut out = format!("Active rubric: {} (v{})\n\nDimensions:\n", r.name, r.version);
        for dim in &r.dimensions {
            out.push_str(&format!(
                "  - {} (weight {:.1}): {}\n",
                dim.label, dim.weight, dim.description
            ));
        }
        out
    }

    fn edit_rubric_hint(&self, agent: &Agent) -> String {
        format!(
            "Your rubric is stored as a YAML file. Edit it directly or use the CLI:\n\n\
             Current rubric: {} (v{})\n\
             Dimensions: {}\n\n\
             To customize, modify your rubric.yaml and reload with `impact-mcp rubric load`.",
            agent.rubric.name,
            agent.rubric.version,
            agent
                .rubric
                .dimension_keys()
                .join(", "),
        )
    }

    fn show_archetypes(&self, agent: &Agent) -> String {
        let readiness = agent.readiness();
        let mut out = String::from("Staff archetype coverage based on your evidence:\n\n");
        for arch in Archetype::ALL {
            let strength = readiness
                .archetype_strength
                .get(arch.label())
                .copied()
                .unwrap_or(0.0);
            let bar = "█".repeat((strength * 20.0) as usize);
            let empty = "░".repeat(20 - (strength * 20.0) as usize);
            out.push_str(&format!(
                "  {:<15} {}{} {:.0}%\n",
                arch.label(),
                bar,
                empty,
                strength * 100.0
            ));
        }

        if let Some(dominant) = agent.archetype_profile.dominant() {
            out.push_str(&format!("\nSelected primary archetype: {dominant}\n"));
        }
        out
    }

    fn suggest_archetype(&self, agent: &Agent) -> String {
        let readiness = agent.readiness();
        let best = readiness
            .archetype_strength
            .iter()
            .max_by(|a, b| a.1.partial_cmp(b.1).unwrap_or(std::cmp::Ordering::Equal));

        match best {
            Some((name, score)) => format!(
                "Based on your evidence, your strongest archetype is {name} ({:.0}%).\n\n\
                 {name} engineers are valued for: {}\n\n\
                 Leaning into this archetype means emphasizing these strengths \
                 in your narrative and seeking work that reinforces this shape.",
                score * 100.0,
                Archetype::ALL
                    .iter()
                    .find(|a| a.label() == name)
                    .map(|a| a.description())
                    .unwrap_or(""),
            ),
            None => "No evidence yet — add some evidence cards to get archetype suggestions.".into(),
        }
    }

    fn show_gaps(&self, agent: &Agent) -> String {
        let readiness = agent.readiness();
        if readiness.gaps.is_empty() {
            return "No gaps identified — you have strong coverage across all rubric dimensions!"
                .into();
        }

        let mut out = format!(
            "Readiness: {:.0}% — gaps against \"{}\":\n\n",
            readiness.overall_score * 100.0,
            agent.rubric.name,
        );
        for gap in &readiness.gaps {
            out.push_str(&format!(
                "  [{:>8}] {}: {}\n",
                gap.coverage.to_string(),
                gap.dimension,
                gap.suggestion,
            ));
        }
        out
    }

    fn update_packet(&self, agent: &Agent) -> String {
        generate_packet_section(agent)
    }

    fn draft_status(&self, agent: &Agent) -> String {
        draft_status_update(agent)
    }

    fn explain_readiness(&self, agent: &Agent) -> String {
        let readiness = agent.readiness();
        let mut out = format!(
            "Overall readiness score: {:.0}%\n\nDimension breakdown:\n",
            readiness.overall_score * 100.0,
        );
        for dim in &agent.rubric.dimensions {
            let level = readiness
                .dimension_coverage
                .get(&dim.key)
                .map(|l| l.to_string())
                .unwrap_or_else(|| "Unknown".into());
            out.push_str(&format!("  {:<12} {}\n", dim.label, level));
        }

        out.push_str(&format!(
            "\n{} gap(s) identified. Ask \"what am I missing?\" for details.",
            readiness.gaps.len()
        ));
        out
    }

    fn help(&self) -> String {
        "impact-mcp — available commands:\n\n\
         \x20 \"What rubric am I using?\"\n\
         \x20 \"Edit my rubric\"\n\
         \x20 \"What Staff archetypes am I currently showing?\"\n\
         \x20 \"Which archetype should I lean into?\"\n\
         \x20 \"What am I missing?\"\n\
         \x20 \"Update my promotion packet\"\n\
         \x20 \"Draft my weekly status update\"\n\
         \x20 \"Explain my readiness score\"\n\
         \x20 \"help\" — show this message\n\
         \x20 \"quit\" — exit\n\n\
         Any other input is forwarded to the LLM for freeform conversation\n\
         (requires an API key, e.g. ANTHROPIC_API_KEY).\n"
            .into()
    }
}
