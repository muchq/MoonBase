//! One request in, one event out: tokenize, predict, score, learn.

use std::{collections::VecDeque, sync::Arc};

use caddylog::CaddyLine;
use serde::{Deserialize, Serialize};

use crate::{
    bigram::Bigram,
    lanes::{DEFAULT_LANES, Lanes},
    score::{Scorer, Verdict, WARMUP},
    token::{BOS, DEFAULT_CAP, Vocab, token_text},
};

/// How many past events `recent` can hand back.
pub const RING: usize = 200;
/// Guesses per predictor in an event.
pub const TOP_K: usize = 5;

/// The event every consumer reads: the muchq.com page over the stream,
/// games_hub through `recent`. Field names are the wire; `api.rs` pins
/// the raw JSON. `step`, `threshold` and `ewma_loss` are the baseline the
/// verdict was judged against, before the event itself was learned, so
/// `verdict` is `anomaly` exactly when `surprise.bigram > threshold`.
#[derive(Clone, Debug, Serialize, PartialEq)]
pub struct Event {
    pub seq: u64,
    pub ts: f64,
    pub lane: usize,
    pub step: u64,
    pub context: Vec<String>,
    pub actual: String,
    pub predictions: PerPredictor<Vec<Guess>>,
    pub surprise: PerPredictor<f64>,
    /// `None` while the scorer is warming up.
    pub threshold: Option<f64>,
    pub verdict: Verdict,
    pub ewma_loss: PerPredictor<f64>,
    pub vocab_size: usize,
}

#[derive(Clone, Debug, Serialize, PartialEq)]
pub struct Guess {
    pub token: String,
    pub p: f64,
}

/// One value per predictor; the network's column is null until it exists.
#[derive(Clone, Debug, Serialize, PartialEq)]
pub struct PerPredictor<T> {
    pub bigram: T,
    pub net: Option<T>,
}

/// What `state` reports.
#[derive(Clone, Debug, Serialize, PartialEq)]
pub struct StateView {
    pub seq: u64,
    pub step: u64,
    pub vocab_size: usize,
    pub vocab_cap: usize,
    pub warmup_needed: u64,
    pub ewma_loss: PerPredictor<f64>,
    pub threshold: Option<f64>,
    pub anomalies: u64,
    pub novelties: u64,
}

/// The learned state a checkpoint carries; lanes and the ring are not in
/// it, since a restart's clients and its page both start fresh.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct Snapshot {
    pub seq: u64,
    pub vocab: Vec<String>,
    pub bigram: Bigram,
    pub scorer: Scorer,
    pub anomalies: u64,
    pub novelties: u64,
}

pub struct Engine {
    vocab: Vocab,
    lanes: Lanes,
    bigram: Bigram,
    scorer: Scorer,
    seq: u64,
    anomalies: u64,
    novelties: u64,
    ring: VecDeque<Arc<Event>>,
}

impl Default for Engine {
    fn default() -> Self {
        Self::new(DEFAULT_CAP, DEFAULT_LANES)
    }
}

impl Engine {
    pub fn new(vocab_cap: usize, lanes: usize) -> Self {
        Self {
            vocab: Vocab::new(vocab_cap),
            lanes: Lanes::new(lanes),
            bigram: Bigram::default(),
            scorer: Scorer::default(),
            seq: 0,
            anomalies: 0,
            novelties: 0,
            ring: VecDeque::with_capacity(RING),
        }
    }

    pub fn from_snapshot(snapshot: Snapshot, vocab_cap: usize, lanes: usize) -> Self {
        Self {
            vocab: Vocab::from_names(snapshot.vocab, vocab_cap),
            lanes: Lanes::new(lanes),
            bigram: snapshot.bigram,
            scorer: snapshot.scorer,
            seq: snapshot.seq,
            anomalies: snapshot.anomalies,
            novelties: snapshot.novelties,
            ring: VecDeque::with_capacity(RING),
        }
    }

    pub fn snapshot(&self) -> Snapshot {
        Snapshot {
            seq: self.seq,
            vocab: self.vocab.names().to_vec(),
            bigram: self.bigram.clone(),
            scorer: self.scorer.clone(),
            anomalies: self.anomalies,
            novelties: self.novelties,
        }
    }

    /// Scores the request against what the client's last token predicted,
    /// then learns it. A token seen for the first time is novel rather than
    /// judged, and stays out of the baseline: its surprise says nothing
    /// about the sequence, only about the vocabulary.
    pub fn ingest(&mut self, line: &CaddyLine) -> Arc<Event> {
        let (token, novel) = self.vocab.intern(&token_text(line));
        let lane = self.lanes.touch(line.client_ip());
        let window = self.lanes.window(lane);
        let prev = window.back().copied().unwrap_or(BOS);
        let context = window
            .iter()
            .map(|&id| self.vocab.name(id).to_string())
            .collect();

        let vocab_size = self.vocab.len();
        let guesses = self
            .bigram
            .top(prev, vocab_size, TOP_K)
            .into_iter()
            .map(|(id, p)| Guess {
                token: self.vocab.name(id).to_string(),
                p,
            })
            .collect();
        let surprise = -self.bigram.probability(prev, token, vocab_size).ln();
        let step = self.scorer.steps();
        let threshold = self.scorer.threshold();
        let ewma_loss = self.ewma_loss();
        let verdict = if novel {
            self.novelties += 1;
            Verdict::Novel
        } else {
            let verdict = self.scorer.judge(surprise);
            if verdict == Verdict::Anomaly {
                self.anomalies += 1;
            }
            self.scorer.observe(surprise);
            verdict
        };

        self.bigram.observe(prev, token);
        self.lanes.push(lane, token);
        self.seq += 1;

        let event = Arc::new(Event {
            seq: self.seq,
            ts: line.ts,
            lane,
            step,
            context,
            actual: self.vocab.name(token).to_string(),
            predictions: PerPredictor {
                bigram: guesses,
                net: None,
            },
            surprise: PerPredictor {
                bigram: surprise,
                net: None,
            },
            threshold,
            verdict,
            ewma_loss,
            vocab_size,
        });
        if self.ring.len() == RING {
            self.ring.pop_front();
        }
        self.ring.push_back(Arc::clone(&event));
        event
    }

    /// Events after `after`, oldest first, from the ring.
    pub fn recent(&self, after: u64) -> Vec<Arc<Event>> {
        self.ring
            .iter()
            .filter(|event| event.seq > after)
            .cloned()
            .collect()
    }

    pub fn state(&self) -> StateView {
        StateView {
            seq: self.seq,
            step: self.scorer.steps(),
            vocab_size: self.vocab.len(),
            vocab_cap: self.vocab.cap(),
            warmup_needed: WARMUP,
            ewma_loss: self.ewma_loss(),
            threshold: self.scorer.threshold(),
            anomalies: self.anomalies,
            novelties: self.novelties,
        }
    }

    fn ewma_loss(&self) -> PerPredictor<f64> {
        PerPredictor {
            bigram: self.scorer.mean(),
            net: None,
        }
    }
}

#[cfg(test)]
pub(crate) mod fixtures {
    use caddylog::CaddyLine;

    /// A request line as Caddy writes it, for one client.
    pub fn request(ts: f64, ip: &str, method: &str, uri: &str, status: u16, ua: &str) -> CaddyLine {
        let json = format!(
            r#"{{"ts":{ts},"status":{status},"request":{{"host":"api.muchq.com","method":"{method}","uri":"{uri}","client_ip":"{ip}","headers":{{"User-Agent":["{ua}"]}}}}}}"#
        );
        CaddyLine::parse(json.as_bytes()).unwrap()
    }

    pub const BROWSER: &str = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/126.0";
    pub const CURL: &str = "curl/8.6.0";
}

#[cfg(test)]
mod tests {
    use super::{fixtures::*, *};

    fn browser_redirect(ts: f64, ip: &str) -> CaddyLine {
        request(ts, ip, "GET", "/iili/v1/r/abc", 302, BROWSER)
    }

    #[test]
    fn the_first_sight_of_a_token_is_novel_and_the_second_is_judged() {
        let mut engine = Engine::default();
        let first = engine.ingest(&browser_redirect(1.0, "1.1.1.1"));
        assert_eq!(first.verdict, Verdict::Novel);
        assert_eq!(first.actual, "api.muchq.com GET /iili/v1/r/* 302 browser");
        assert!(first.context.is_empty(), "a new lane has no history");
        let second = engine.ingest(&browser_redirect(2.0, "1.1.1.1"));
        assert_eq!(second.verdict, Verdict::Warmup);
        assert_eq!(second.context, std::slice::from_ref(&first.actual));
        assert_eq!(second.step, 0, "novelty stays out of the baseline");
        assert_eq!(second.seq, 2);
        let state = engine.state();
        assert_eq!(state.novelties, 1);
        assert_eq!(state.step, 1);
    }

    #[test]
    fn a_learned_transition_is_predicted_and_a_new_one_scores_higher() {
        let mut engine = Engine::default();
        for i in 0..20 {
            engine.ingest(&request(
                f64::from(i),
                "1.1.1.1",
                "GET",
                "/iili/v1/r/x",
                302,
                BROWSER,
            ));
        }
        let expected = engine.ingest(&browser_redirect(21.0, "1.1.1.1"));
        assert_eq!(expected.predictions.bigram[0].token, expected.actual);
        assert!(expected.predictions.bigram[0].p > 0.9);
        assert_eq!(expected.predictions.net, None);
        assert_eq!(expected.surprise.net, None);
        // The scanner's first probe is novel; its second, in a lane whose
        // history is a probe, is judged against the probe's row.
        engine.ingest(&request(22.0, "9.9.9.9", "GET", "/.env", 404, CURL));
        engine.ingest(&request(23.0, "9.9.9.9", "GET", "/wp-login.php", 404, CURL));
        let odd = engine.ingest(&request(24.0, "1.1.1.1", "GET", "/wp-login.php", 404, CURL));
        assert_ne!(odd.verdict, Verdict::Novel, "the token exists by now");
        assert!(odd.surprise.bigram > expected.surprise.bigram);
    }

    // The prediction hangs on the lane's latest token, not its oldest.
    #[test]
    fn the_previous_token_is_the_newest_in_the_window() {
        let mut engine = Engine::default();
        let teach = |engine: &mut Engine, ip: &str| {
            for i in 0..8 {
                engine.ingest(&request(
                    f64::from(i),
                    ip,
                    "GET",
                    "/iili/v1/r/x",
                    302,
                    BROWSER,
                ));
            }
            engine.ingest(&request(8.0, ip, "GET", "/.env", 404, CURL));
        };
        teach(&mut engine, "1.1.1.1");
        let probe_then = engine.ingest(&request(9.0, "1.1.1.1", "GET", "/wp-login.php", 404, CURL));
        teach(&mut engine, "2.2.2.2");
        let again = engine.ingest(&request(9.0, "2.2.2.2", "GET", "/wp-login.php", 404, CURL));
        assert_eq!(again.predictions.bigram[0].token, probe_then.actual);
        assert!(again.surprise.bigram < 1.0, "{}", again.surprise.bigram);
    }

    #[test]
    fn after_warmup_a_break_in_a_regular_lane_is_an_anomaly() {
        let mut engine = Engine::default();
        let mut last = None;
        for i in 0..(WARMUP + 5) {
            last = Some(engine.ingest(&browser_redirect(i as f64, "1.1.1.1")));
        }
        let last = last.unwrap();
        assert_eq!(last.verdict, Verdict::Expected);
        assert!(last.surprise.bigram <= last.threshold.unwrap());
        // Teach the vocabulary the probe on another lane first, so the
        // anomaly is the transition and not the novelty.
        engine.ingest(&request(9000.0, "9.9.9.9", "GET", "/.env", 404, CURL));
        let before = engine.state();
        let broken = engine.ingest(&request(9001.0, "1.1.1.1", "GET", "/.env", 404, CURL));
        assert_eq!(broken.verdict, Verdict::Anomaly);
        // The threshold on the event is the one that judged it, not the one
        // the event moved.
        assert!(broken.surprise.bigram > broken.threshold.unwrap());
        assert_eq!(broken.threshold, before.threshold);
        assert_eq!(broken.step, before.step);
        let after = engine.state();
        assert!(after.threshold > before.threshold);
        assert_eq!(after.anomalies, 1);
    }

    #[test]
    fn recent_hands_back_the_ring_after_a_sequence_number() {
        let mut engine = Engine::default();
        for i in 0..(RING as u64 + 10) {
            engine.ingest(&browser_redirect(i as f64, "1.1.1.1"));
        }
        let all = engine.recent(0);
        assert_eq!(all.len(), RING);
        assert_eq!(all[0].seq, 11);
        assert_eq!(all.last().unwrap().seq, RING as u64 + 10);
        let tail = engine.recent(RING as u64 + 7);
        assert_eq!(
            tail.iter().map(|e| e.seq).collect::<Vec<_>>(),
            [208, 209, 210]
        );
        assert!(engine.recent(RING as u64 + 10).is_empty());
    }

    #[test]
    fn a_snapshot_restores_the_learned_state_and_continues_the_sequence() {
        let mut engine = Engine::default();
        for i in 0..30 {
            engine.ingest(&browser_redirect(f64::from(i), "1.1.1.1"));
        }
        engine.ingest(&request(31.0, "9.9.9.9", "GET", "/.env", 404, CURL));
        let snapshot = engine.snapshot();
        let json = serde_json::to_string(&snapshot).unwrap();
        let restored: Snapshot = serde_json::from_str(&json).unwrap();
        assert_eq!(restored, snapshot);
        let mut back = Engine::from_snapshot(restored, DEFAULT_CAP, DEFAULT_LANES);
        assert_eq!(back.state(), StateView { ..engine.state() });
        assert!(
            back.recent(0).is_empty(),
            "the ring is not in the checkpoint"
        );
        let next = back.ingest(&browser_redirect(32.0, "1.1.1.1"));
        assert_eq!(next.seq, 32);
        assert_ne!(next.verdict, Verdict::Novel, "the vocabulary came back");
        assert!(next.context.is_empty(), "lanes did not");
        assert_eq!(
            next.predictions.bigram[0].token, next.actual,
            "the counts came back"
        );
    }
}
