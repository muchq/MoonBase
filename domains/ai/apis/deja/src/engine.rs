//! One request in, one event out: tokenize, predict, score, learn.

use std::{collections::VecDeque, fmt, sync::Arc};

use base64::{Engine as _, engine::general_purpose::STANDARD as BASE64};
use caddylog::CaddyLine;
use serde::{Deserialize, Serialize};

use crate::{
    bigram::Bigram,
    lanes::{DEFAULT_LANES, Lanes, WINDOW},
    net::{DEFAULT_SEED, Net},
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

/// One value per predictor.
#[derive(Clone, Debug, Serialize, PartialEq)]
pub struct PerPredictor<T> {
    pub bigram: T,
    pub net: T,
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
/// it, since a restart's clients and its page both start fresh. The net
/// rides in the same file as the vocabulary so token ids and weights
/// cannot drift apart; a checkpoint without one starts the net fresh.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct Snapshot {
    pub seq: u64,
    pub vocab: Vec<String>,
    pub bigram: Bigram,
    pub scorer: Scorer,
    pub anomalies: u64,
    pub novelties: u64,
    #[serde(default)]
    pub net: Option<NetSnapshot>,
}

/// The net's weights as base64 safetensors, and its own baseline.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct NetSnapshot {
    pub weights: String,
    pub scorer: Scorer,
}

/// Why `next` refused a context.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum NextError {
    UnknownToken(String),
    Length(usize),
}

impl fmt::Display for NextError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            NextError::UnknownToken(name) => write!(f, "unknown token {name:?}"),
            NextError::Length(n) => write!(f, "context has {n} tokens; 1 to {WINDOW} are allowed"),
        }
    }
}

pub struct Engine {
    vocab: Vocab,
    lanes: Lanes,
    bigram: Bigram,
    scorer: Scorer,
    net: Net,
    net_scorer: Scorer,
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
        Self::with_net(vocab_cap, lanes, Net::new(vocab_cap, DEFAULT_SEED))
    }

    fn with_net(vocab_cap: usize, lanes: usize, net: Net) -> Self {
        Self {
            vocab: Vocab::new(vocab_cap),
            lanes: Lanes::new(lanes),
            bigram: Bigram::default(),
            scorer: Scorer::default(),
            net,
            net_scorer: Scorer::default(),
            seq: 0,
            anomalies: 0,
            novelties: 0,
            ring: VecDeque::with_capacity(RING),
        }
    }

    /// A net that will not load — none in the checkpoint, or weights that
    /// are not this net's — starts fresh with its baseline; the rest of
    /// the checkpoint is kept either way.
    pub fn from_snapshot(snapshot: Snapshot, vocab_cap: usize, lanes: usize) -> Self {
        let (net, net_scorer) = match snapshot.net {
            None => (Net::new(vocab_cap, DEFAULT_SEED), Scorer::default()),
            Some(saved) => match BASE64
                .decode(&saved.weights)
                .map_err(|error| error.to_string())
                .and_then(|bytes| {
                    Net::from_weights(vocab_cap, DEFAULT_SEED, &bytes)
                        .map_err(|error| error.to_string())
                }) {
                Ok(net) => (net, saved.scorer),
                Err(error) => {
                    tracing::error!(%error, "checkpoint's net unusable; starting it fresh");
                    (Net::new(vocab_cap, DEFAULT_SEED), Scorer::default())
                }
            },
        };
        Self {
            vocab: Vocab::from_names(snapshot.vocab, vocab_cap),
            lanes: Lanes::new(lanes),
            bigram: snapshot.bigram,
            scorer: snapshot.scorer,
            net,
            net_scorer,
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
            net: Some(NetSnapshot {
                weights: BASE64.encode(self.net.weights()),
                scorer: self.net_scorer.clone(),
            }),
        }
    }

    /// Both predictors' guesses after `context`, a list of one to
    /// `WINDOW` token names. Nothing is learned and no event is made.
    pub fn next(&self, context: &[String]) -> Result<PerPredictor<Vec<Guess>>, NextError> {
        if context.is_empty() || context.len() > WINDOW {
            return Err(NextError::Length(context.len()));
        }
        let ids = context
            .iter()
            .map(|name| {
                self.vocab
                    .id(name)
                    .ok_or_else(|| NextError::UnknownToken(name.clone()))
            })
            .collect::<Result<Vec<u16>, _>>()?;
        let prev = *ids.last().expect("at least one token");
        Ok(PerPredictor {
            bigram: self.guesses(self.bigram.top(prev, self.vocab.len(), TOP_K)),
            net: self.guesses(self.net.predict(&ids).top(self.vocab.len(), TOP_K)),
        })
    }

    fn guesses(&self, ranked: Vec<(u16, f64)>) -> Vec<Guess> {
        ranked
            .into_iter()
            .map(|(id, p)| Guess {
                token: self.vocab.name(id).to_string(),
                p,
            })
            .collect()
    }

    /// Scores the request against what the client's history predicted,
    /// then learns it. A token seen for the first time is novel rather than
    /// judged, and stays out of both baselines: its surprise says nothing
    /// about the sequence, only about the vocabulary. The bigram's scorer
    /// decides the verdict; the net's only keeps its own baseline.
    pub fn ingest(&mut self, line: &CaddyLine) -> Arc<Event> {
        let (token, novel) = self.vocab.intern(&token_text(line));
        let lane = self.lanes.touch(line.client_ip());
        let window: Vec<u16> = self.lanes.window(lane).iter().copied().collect();
        let prev = window.last().copied().unwrap_or(BOS);
        let context = window
            .iter()
            .map(|&id| self.vocab.name(id).to_string())
            .collect();

        let vocab_size = self.vocab.len();
        let guesses = self.guesses(self.bigram.top(prev, vocab_size, TOP_K));
        let surprise = -self.bigram.probability(prev, token, vocab_size).ln();
        let net_prediction = self.net.predict(&window);
        let net_guesses = self.guesses(net_prediction.top(vocab_size, TOP_K));
        let net_surprise = net_prediction.surprise(token);
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
            self.net_scorer.observe(net_surprise);
            verdict
        };

        self.bigram.observe(prev, token);
        self.net.learn(&window, token, verdict != Verdict::Anomaly);
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
                net: net_guesses,
            },
            surprise: PerPredictor {
                bigram: surprise,
                net: net_surprise,
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
            net: self.net_scorer.mean(),
        }
    }
}

#[cfg(test)]
pub(crate) mod fixtures {
    use caddylog::CaddyLine;

    use super::*;

    /// The default engine with a zeroed net: its numbers are exact.
    pub fn zeroed() -> Engine {
        Engine::with_net(DEFAULT_CAP, DEFAULT_LANES, Net::zeroed(DEFAULT_CAP))
    }

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
        assert_eq!(
            expected.predictions.net.len(),
            2,
            "top five of the three known tokens, BOS never guessed"
        );
        assert!(expected.surprise.net.is_finite() && expected.surprise.net >= 0.0);
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
        // Learned once, never replayed: the ring's newest pair is still the
        // probe's novel sighting on the fresh lane, whose context is all
        // BOS; the anomaly's context of redirects never went in.
        let (context, target) = engine.net.replay_newest().unwrap();
        assert_eq!(target, engine.vocab.id(&broken.actual).unwrap());
        assert_eq!(context, [BOS; crate::net::CONTEXT]);
        // The threshold on the event is the one that judged it, not the one
        // the event moved.
        assert!(broken.surprise.bigram > broken.threshold.unwrap());
        assert_eq!(broken.threshold, before.threshold);
        assert_eq!(broken.step, before.step);
        let after = engine.state();
        assert!(after.threshold > before.threshold);
        assert_eq!(after.anomalies, 1);
    }

    // The net's baseline is its own scorer over its own surprise, on the
    // same terms as the bigram's: judged events only, first sight is not
    // an observation. The bigram's threshold is the one the verdict uses.
    #[test]
    fn the_net_scorer_observes_judged_events_and_skips_novelty() {
        let mut engine = Engine::default();
        let novel = engine.ingest(&browser_redirect(1.0, "1.1.1.1"));
        assert_eq!(novel.verdict, Verdict::Novel);
        assert!(novel.surprise.net > 0.0);
        assert_eq!(engine.state().ewma_loss.net, 0.0, "novelty is not observed");
        let judged = engine.ingest(&browser_redirect(2.0, "1.1.1.1"));
        assert_eq!(
            judged.ewma_loss,
            PerPredictor {
                bigram: 0.0,
                net: 0.0
            },
            "the baseline the event was judged against"
        );
        let state = engine.state();
        assert_eq!(state.ewma_loss.bigram, judged.surprise.bigram);
        assert_eq!(
            state.ewma_loss.net, judged.surprise.net,
            "the first observation is the baseline"
        );
        assert_ne!(state.ewma_loss.net, state.ewma_loss.bigram);
    }

    #[test]
    fn the_net_learns_a_lane_and_predicts_its_next_token() {
        let mut engine = Engine::default();
        let mut last = None;
        for i in 0..120 {
            let (uri, status) = if i % 2 == 0 {
                ("/iili/v1/r/x", 302)
            } else {
                ("/iili/v1/shorten", 200)
            };
            last = Some(engine.ingest(&request(
                f64::from(i),
                "1.1.1.1",
                "GET",
                uri,
                status,
                BROWSER,
            )));
        }
        let last = last.unwrap();
        assert_eq!(last.predictions.net[0].token, last.actual);
        assert!(
            last.predictions.net[0].p > 0.5,
            "{}",
            last.predictions.net[0].p
        );
        assert!(last.surprise.net < 1.0, "{}", last.surprise.net);
        assert!(
            last.predictions.net.iter().all(|g| g.token != "<bos>"),
            "{:?}",
            last.predictions.net
        );
    }

    #[test]
    fn next_answers_both_predictors_for_a_context_of_names() {
        let mut engine = Engine::default();
        for i in 0..20 {
            engine.ingest(&browser_redirect(f64::from(i), "1.1.1.1"));
        }
        let name = "api.muchq.com GET /iili/v1/r/* 302 browser".to_string();
        let both = engine.next(&[name.clone(), name.clone()]).unwrap();
        assert_eq!(both.bigram[0].token, name);
        assert_eq!(both.bigram.len(), 1, "the bigram lists what it has seen");
        assert_eq!(both.net[0].token, name);
        assert_eq!(both.net.len(), 2, "<unk> and the token; never <bos>");
        assert_eq!(
            engine.next(&["nope".to_string()]),
            Err(NextError::UnknownToken("nope".to_string()))
        );
        assert_eq!(engine.next(&[]), Err(NextError::Length(0)));
        assert_eq!(
            engine.next(&vec![name; WINDOW + 1]),
            Err(NextError::Length(WINDOW + 1))
        );
        assert_eq!(
            engine.state().seq,
            20,
            "asking is not an event and teaches nothing"
        );
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
        // The weights came back: a lane with no history reads the same in
        // both engines before either learns again. (After that they part:
        // the replay ring and the optimizer's moments are not checkpointed.)
        let fresh_lane = engine.ingest(&browser_redirect(32.0, "2.2.2.2"));
        let same_lane = back.ingest(&browser_redirect(32.0, "2.2.2.2"));
        assert_eq!(fresh_lane.predictions.net, same_lane.predictions.net);
        assert_eq!(fresh_lane.surprise.net, same_lane.surprise.net);
        let next = back.ingest(&browser_redirect(33.0, "1.1.1.1"));
        assert_eq!(next.seq, 33);
        assert_ne!(next.verdict, Verdict::Novel, "the vocabulary came back");
        assert!(next.context.is_empty(), "lanes did not");
        assert_eq!(
            next.predictions.bigram[0].token, next.actual,
            "the counts came back"
        );
    }

    // Phase 2's checkpoint, verbatim: no `net` key. The bigram and the
    // vocabulary are kept and the net starts fresh beside them.
    #[test]
    fn a_checkpoint_without_a_net_keeps_the_bigram_and_starts_the_net_fresh() {
        let json = r#"{"seq":3,"vocab":["<unk>","<bos>","api.muchq.com GET /iili/v1/r/* 302 browser"],"bigram":{"rows":{"1":{"total":1,"next":{"2":1}},"2":{"total":2,"next":{"2":2}}}},"scorer":{"steps":2,"mean":0.4,"var":0.0},"anomalies":0,"novelties":1}"#;
        let snapshot: Snapshot = serde_json::from_str(json).unwrap();
        assert_eq!(snapshot.net, None);
        let mut engine = Engine::from_snapshot(snapshot, DEFAULT_CAP, DEFAULT_LANES);
        let state = engine.state();
        assert_eq!(state.seq, 3);
        assert_eq!(state.vocab_size, 3);
        assert_eq!(state.ewma_loss.bigram, 0.4);
        assert_eq!(state.ewma_loss.net, 0.0);
        let next = engine.ingest(&browser_redirect(4.0, "1.1.1.1"));
        assert_ne!(next.verdict, Verdict::Novel);
        assert_eq!(next.predictions.bigram[0].token, next.actual);
        assert_eq!(next.predictions.net.len(), 2);
        assert!(
            (next.surprise.net - (DEFAULT_CAP as f64).ln()).abs() < 1.0,
            "a fresh net is near uniform: {}",
            next.surprise.net
        );
        assert!(
            engine.snapshot().net.is_some(),
            "the next checkpoint carries it"
        );
    }

    // A net blob that will not load is the same as none: the bigram is
    // kept and the net starts fresh, rather than the service staying down.
    #[test]
    fn a_net_that_will_not_load_starts_fresh_and_keeps_the_rest() {
        let mut engine = Engine::default();
        for i in 0..10 {
            engine.ingest(&browser_redirect(f64::from(i), "1.1.1.1"));
        }
        let mut snapshot = engine.snapshot();
        snapshot.net.as_mut().unwrap().weights = "bm90IHNhZmV0ZW5zb3Jz".into();
        let mut back = Engine::from_snapshot(snapshot, DEFAULT_CAP, DEFAULT_LANES);
        assert_eq!(
            back.state().ewma_loss.net,
            0.0,
            "the net's scorer went with it"
        );
        assert_eq!(
            back.state().ewma_loss.bigram,
            engine.state().ewma_loss.bigram
        );
        let next = back.ingest(&browser_redirect(11.0, "1.1.1.1"));
        assert_eq!(next.predictions.bigram[0].token, next.actual);
        assert!(
            (next.surprise.net - (DEFAULT_CAP as f64).ln()).abs() < 1.0,
            "{}",
            next.surprise.net
        );
    }

    #[test]
    fn a_zeroed_engine_is_exactly_uniform_for_the_wire_pins() {
        let mut engine = fixtures::zeroed();
        let first = engine.ingest(&browser_redirect(1.0, "1.1.1.1"));
        assert_eq!(first.surprise.net, f64::from((DEFAULT_CAP as f32).ln()));
    }
}
