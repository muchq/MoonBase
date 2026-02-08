use chrono::Utc;

use crate::agent::Agent;

/// Draft a weekly status update from recent evidence.
///
/// The status update highlights recent impact using language aligned
/// with the active rubric and archetypes. It is always a *draft* —
/// never sent automatically.
pub fn draft_status_update(agent: &Agent) -> String {
    let now = Utc::now();
    let mut out = format!(
        "── Weekly Status Draft ({}) ──\n\n",
        now.format("%Y-%m-%d"),
    );

    let cards = agent.store.all();
    if cards.is_empty() {
        out.push_str(
            "No evidence cards yet. Add evidence from your integrations \
             or manually to generate a status update.\n",
        );
        return out;
    }

    // Sort by timestamp descending — most recent first
    let mut recent: Vec<_> = cards;
    recent.sort_by(|a, b| b.timestamp.cmp(&a.timestamp));

    // Take the 7 most recent items as a reasonable week window
    let week_items: Vec<_> = recent.into_iter().take(7).collect();

    out.push_str("Key accomplishments this week:\n\n");
    for card in &week_items {
        let tags = if card.rubric_tags.is_empty() {
            String::new()
        } else {
            format!(" [{}]", card.rubric_tags.join(", "))
        };
        out.push_str(&format!("  • {}{}\n", card.summary, tags));
    }

    // Archetype-aligned narrative hint
    if let Some(dominant) = agent.archetype_profile.dominant() {
        out.push_str(&format!(
            "\nNarrative angle ({}): Frame updates around {} to reinforce \
             your Staff shape.\n",
            dominant.label(),
            dominant.description().to_lowercase(),
        ));
    }

    out.push_str("\n── END DRAFT ──\n");
    out
}
