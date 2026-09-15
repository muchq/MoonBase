//! The control predictor: next-token counts keyed by the previous token,
//! smoothed so an unseen pair still has a probability to be surprised by.

use std::collections::HashMap;

use serde::{Deserialize, Serialize};

/// Additive smoothing: the mass an unseen next token gets, in counts.
pub const ALPHA: f64 = 0.1;
/// Successors a row keeps. A row that is full forgets its rarest successor
/// to admit a new one, which bounds the table at `vocab × MAX_SUCCESSORS`
/// however a scanner sequences its requests.
pub const MAX_SUCCESSORS: usize = 64;

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct Bigram {
    rows: HashMap<u16, Row>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
struct Row {
    total: u32,
    next: HashMap<u16, u32>,
}

impl Bigram {
    /// `(count(prev, next) + α) / (count(prev) + α·V)` over a vocabulary of
    /// `vocab_size` tokens, so a never-seen row is uniform and a seen one
    /// still leaves every other token a sliver.
    pub fn probability(&self, prev: u16, next: u16, vocab_size: usize) -> f64 {
        let (count, total) = match self.rows.get(&prev) {
            Some(row) => (row.next.get(&next).copied().unwrap_or(0), row.total),
            None => (0, 0),
        };
        (f64::from(count) + ALPHA) / (f64::from(total) + ALPHA * vocab_size as f64)
    }

    /// The `k` likeliest next tokens after `prev`, likeliest first, ties by
    /// id so the order is stable. Only tokens seen after `prev` are listed;
    /// the rest share the smoothing mass and would tie at the bottom.
    pub fn top(&self, prev: u16, vocab_size: usize, k: usize) -> Vec<(u16, f64)> {
        let Some(row) = self.rows.get(&prev) else {
            return Vec::new();
        };
        let mut ranked: Vec<(u16, u32)> = row.next.iter().map(|(&id, &c)| (id, c)).collect();
        ranked.sort_by(|a, b| b.1.cmp(&a.1).then(a.0.cmp(&b.0)));
        ranked
            .into_iter()
            .take(k)
            .map(|(id, _)| (id, self.probability(prev, id, vocab_size)))
            .collect()
    }

    pub fn observe(&mut self, prev: u16, next: u16) {
        let row = self.rows.entry(prev).or_default();
        if !row.next.contains_key(&next) && row.next.len() >= MAX_SUCCESSORS {
            let (&rarest, &count) = row
                .next
                .iter()
                .min_by_key(|&(&id, &count)| (count, id))
                .expect("a full row is not empty");
            row.next.remove(&rarest);
            row.total -= count;
        }
        row.total += 1;
        *row.next.entry(next).or_default() += 1;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn an_unseen_row_is_uniform_and_a_seen_one_sums_to_one() {
        let mut bigram = Bigram::default();
        let v = 4;
        assert!((bigram.probability(1, 2, v) - 0.25).abs() < 1e-12);
        bigram.observe(1, 2);
        bigram.observe(1, 2);
        bigram.observe(1, 3);
        let sum: f64 = (0..4).map(|next| bigram.probability(1, next, v)).sum();
        assert!((sum - 1.0).abs() < 1e-12, "{sum}");
        assert!(bigram.probability(1, 2, v) > bigram.probability(1, 3, v));
        assert!(bigram.probability(1, 3, v) > bigram.probability(1, 0, v));
        assert!(bigram.probability(1, 0, v) > 0.0);
    }

    #[test]
    fn top_ranks_by_count_then_id_and_lists_only_what_was_seen() {
        let mut bigram = Bigram::default();
        assert!(bigram.top(1, 8, 5).is_empty());
        for _ in 0..3 {
            bigram.observe(1, 5);
        }
        bigram.observe(1, 2);
        bigram.observe(1, 7);
        let top = bigram.top(1, 8, 5);
        let ids: Vec<u16> = top.iter().map(|(id, _)| *id).collect();
        assert_eq!(ids, [5, 2, 7]);
        assert!(top[0].1 > top[1].1 && top[1].1 == top[2].1);
        assert_eq!(bigram.top(1, 8, 2).len(), 2);
    }

    // Eviction keeps the row's total honest: the forgotten successor's
    // count leaves with it, so the survivors' probabilities still sum to one
    // with the smoothing mass.
    #[test]
    fn a_full_row_forgets_its_rarest_successor() {
        let mut bigram = Bigram::default();
        let v = 4096;
        for id in 0..MAX_SUCCESSORS as u16 {
            bigram.observe(1, id);
            bigram.observe(1, id);
        }
        bigram.observe(1, 7);
        // Every successor has count 2 except 7 at 3; the rarest by (count,
        // id) is 0, and only a new successor evicts.
        bigram.observe(1, 7);
        assert_eq!(bigram.top(1, v, 1)[0].0, 7);
        assert!(bigram.probability(1, 0, v) > bigram.probability(1, 9999, v));
        bigram.observe(1, 4000);
        assert_eq!(
            bigram.probability(1, 0, v),
            bigram.probability(1, 9999, v),
            "successor 0 was not forgotten"
        );
        let row = &bigram.rows[&1];
        assert_eq!(row.next.len(), MAX_SUCCESSORS);
        assert_eq!(row.total, row.next.values().sum::<u32>());
        let seen: f64 = row.next.keys().map(|&n| bigram.probability(1, n, v)).sum();
        let unseen = (v - MAX_SUCCESSORS) as f64 * bigram.probability(1, 9999, v);
        assert!((seen + unseen - 1.0).abs() < 1e-9, "{}", seen + unseen);
    }

    #[test]
    fn counts_survive_a_json_round_trip() {
        let mut bigram = Bigram::default();
        bigram.observe(1, 2);
        bigram.observe(300, 2);
        let json = serde_json::to_string(&bigram).unwrap();
        let back: Bigram = serde_json::from_str(&json).unwrap();
        assert_eq!(back, bigram);
    }
}
