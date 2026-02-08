use crate::agent::Agent;
use crate::archetype::Archetype;
use crate::readiness::CoverageLevel;

/// Generate a promotion-packet section from current evidence and rubric.
///
/// The output is a text draft suitable for pasting into a promotion
/// document. It is always a *draft* — never sent or published
/// automatically.
pub fn generate_packet_section(agent: &Agent) -> String {
    let readiness = agent.readiness();
    let mut out = String::from("═══ PROMOTION PACKET DRAFT ═══\n\n");

    // Summary line
    out.push_str(&format!(
        "Readiness: {:.0}% against \"{}\"\n\n",
        readiness.overall_score * 100.0,
        agent.rubric.name,
    ));

    // Archetype narrative
    if let Some(dominant) = agent.archetype_profile.dominant() {
        out.push_str(&format!(
            "Primary archetype: {} — {}\n\n",
            dominant.label(),
            dominant.description(),
        ));
    }

    // Per-dimension evidence summary
    out.push_str("── Evidence by Dimension ──\n\n");
    for dim in &agent.rubric.dimensions {
        let cards = agent.store.by_rubric_tag(&dim.key);
        let level = readiness
            .dimension_coverage
            .get(&dim.key)
            .copied()
            .unwrap_or(CoverageLevel::None);

        out.push_str(&format!("{}  [{}]\n", dim.label, level));
        if cards.is_empty() {
            out.push_str("  (no evidence yet)\n");
        } else {
            for card in cards.iter().take(5) {
                out.push_str(&format!("  • {} ({})\n", card.summary, card.source));
            }
            let remaining = cards.len().saturating_sub(5);
            if remaining > 0 {
                out.push_str(&format!("  ... and {remaining} more\n"));
            }
        }
        out.push('\n');
    }

    // Archetype strength summary
    out.push_str("── Archetype Strengths ──\n\n");
    for arch in Archetype::ALL {
        let strength = readiness
            .archetype_strength
            .get(arch.label())
            .copied()
            .unwrap_or(0.0);
        out.push_str(&format!("  {:<15} {:.0}%\n", arch.label(), strength * 100.0));
    }

    // Gaps
    if !readiness.gaps.is_empty() {
        out.push_str("\n── Gaps to Address ──\n\n");
        for gap in &readiness.gaps {
            out.push_str(&format!("  • {} ({}): {}\n", gap.dimension, gap.coverage, gap.suggestion));
        }
    }

    out.push_str("\n═══ END DRAFT ═══\n");
    out
}
