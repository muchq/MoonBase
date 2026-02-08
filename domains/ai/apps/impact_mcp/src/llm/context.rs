use crate::archetype::Archetype;
use crate::evidence::EvidenceStore;
use crate::readiness::ReadinessMap;
use crate::rubric::Rubric;

/// Build the system prompt grounding the LLM in the user's evidence,
/// rubric, and archetypes.
///
/// The prompt is rebuilt on agent startup (and could be refreshed on
/// evidence changes in a future version). It gives the model all the
/// context it needs to answer freeform questions about impact and
/// career growth without hallucinating.
pub fn build_system_prompt(store: &EvidenceStore, rubric: &Rubric) -> String {
    let readiness = ReadinessMap::compute(store, rubric);

    let mut prompt = String::from(
        "You are impact-mcp, a local-first AI agent that helps engineers amplify their \
         impact and grow in their role. Doing impactful work leads to better project \
         outcomes and naturally builds the visibility needed for promotion.\n\n\
         Your role:\n\
         - Help the user understand where they are creating impact and where gaps exist\n\
         - Identify which Staff archetypes (tech lead, architect, operator, etc.) their work reflects\n\
         - Suggest concrete work styles and focus areas to close gaps\n\
         - Draft promotion packet content and status updates grounded in real evidence\n\
         - Answer questions conversationally using ONLY the grounded data below\n\n\
         Rules:\n\
         - Never fabricate evidence or claims not supported by the data below\n\
         - Never make people-level judgments beyond artifact analysis\n\
         - Frame advice in terms of the active rubric dimensions\n\
         - Be direct, specific, and actionable\n\
         - When evidence is thin, say so honestly\n\n",
    );

    // Active rubric
    prompt.push_str("═══ ACTIVE RUBRIC ═══\n");
    prompt.push_str(&format!("Name: {} (v{})\n\n", rubric.name, rubric.version));
    for dim in &rubric.dimensions {
        prompt.push_str(&format!(
            "- {} (weight {:.1}): {}\n",
            dim.label, dim.weight, dim.description
        ));
    }

    // Readiness snapshot
    prompt.push_str(&format!(
        "\n═══ READINESS SNAPSHOT ═══\nOverall: {:.0}%\n\n",
        readiness.overall_score * 100.0
    ));
    for dim in &rubric.dimensions {
        let level = readiness
            .dimension_coverage
            .get(&dim.key)
            .map(|l| l.to_string())
            .unwrap_or_else(|| "Unknown".into());
        prompt.push_str(&format!("  {:<12} {}\n", dim.label, level));
    }

    // Gaps
    if !readiness.gaps.is_empty() {
        prompt.push_str("\nIdentified gaps:\n");
        for gap in &readiness.gaps {
            prompt.push_str(&format!("  - {} ({}): {}\n", gap.dimension, gap.coverage, gap.suggestion));
        }
    }

    // Archetype strengths
    prompt.push_str("\n═══ ARCHETYPE STRENGTHS ═══\n");
    for arch in Archetype::ALL {
        let strength = readiness
            .archetype_strength
            .get(arch.label())
            .copied()
            .unwrap_or(0.0);
        prompt.push_str(&format!(
            "  {:<15} {:.0}% — {}\n",
            arch.label(),
            strength * 100.0,
            arch.description(),
        ));
    }

    // Evidence cards
    let cards = store.all();
    prompt.push_str(&format!(
        "\n═══ EVIDENCE STORE ({} cards) ═══\n",
        cards.len()
    ));
    if cards.is_empty() {
        prompt.push_str("(no evidence cards yet)\n");
    } else {
        for card in cards.iter().take(50) {
            prompt.push_str(&format!(
                "\n[{}] {} ({})\n  {}\n",
                card.source,
                card.timestamp.format("%Y-%m-%d"),
                if card.rubric_tags.is_empty() {
                    "untagged".to_string()
                } else {
                    card.rubric_tags.join(", ")
                },
                card.summary,
            ));
            if let Some(link) = &card.link {
                prompt.push_str(&format!("  Link: {link}\n"));
            }
            if !card.archetype_tags.is_empty() {
                let names: Vec<_> = card.archetype_tags.iter().map(|a| a.label()).collect();
                prompt.push_str(&format!("  Archetypes: {}\n", names.join(", ")));
            }
            for excerpt in card.excerpts.iter().take(2) {
                prompt.push_str(&format!("  > {excerpt}\n"));
            }
        }
        if cards.len() > 50 {
            prompt.push_str(&format!("\n... and {} more cards\n", cards.len() - 50));
        }
    }

    prompt
}
