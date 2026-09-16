//! The network predictor: a window MLP over the lane's last eight tokens,
//! trained one step per request, with replay so a night of scanners does
//! not erase the daytime grammar. CPU, f32, ~174k parameters at the
//! default vocabulary cap.

use std::collections::VecDeque;

use candle_core::{D, Device, Module, Result, Tensor, Var};
use candle_nn::{AdamW, Embedding, Linear, Optimizer, ParamsAdamW, VarBuilder, VarMap};

use crate::{lanes::WINDOW, token::BOS};

/// Tokens the net reads; a shorter lane history is left-padded with BOS.
pub const CONTEXT: usize = WINDOW;
pub const EMBED: usize = 16;
pub const HIDDEN: usize = 64;
pub const LEARNING_RATE: f64 = 0.01;
/// Non-anomalous pairs the replay ring holds.
pub const REPLAY: usize = 512;
/// Replayed pairs learned beside each new one.
pub const REPLAYED: usize = 2;
pub const DEFAULT_SEED: u64 = 1150;

pub type Pair = ([u16; CONTEXT], u16);

pub struct Net {
    vocab_cap: usize,
    varmap: VarMap,
    embed: Embedding,
    hidden: Linear,
    out: Linear,
    optimizer: AdamW,
    replay: Replay,
    replayed: usize,
}

/// The net's distribution over the next token for one context.
pub struct Prediction {
    logp: Vec<f32>,
}

impl Prediction {
    /// `-ln p(actual)`.
    pub fn surprise(&self, actual: u16) -> f64 {
        -f64::from(self.logp[usize::from(actual)])
    }

    /// The `k` likeliest of the first `vocab_size` tokens, likeliest first,
    /// ties by id. BOS is never next, so it is never guessed.
    pub fn top(&self, vocab_size: usize, k: usize) -> Vec<(u16, f64)> {
        let mut ranked: Vec<(u16, f32)> = self.logp[..vocab_size.min(self.logp.len())]
            .iter()
            .enumerate()
            .map(|(id, &lp)| (id as u16, lp))
            .filter(|&(id, _)| id != BOS)
            .collect();
        ranked.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap().then(a.0.cmp(&b.0)));
        ranked
            .into_iter()
            .take(k)
            .map(|(id, lp)| (id, f64::from(lp.exp())))
            .collect()
    }
}

impl Net {
    pub fn new(vocab_cap: usize, seed: u64) -> Self {
        Self::with_replay(vocab_cap, seed, REPLAYED)
    }

    /// `replayed` pairs from the ring learned beside each new one; zero is
    /// the no-replay control.
    pub fn with_replay(vocab_cap: usize, seed: u64, replayed: usize) -> Self {
        let mut rng = XorShift::new(seed);
        let varmap = VarMap::new();
        let input = CONTEXT * EMBED;
        // Kaiming-shaped normals from the seeded generator, so a run is
        // reproducible; candle's own initializers draw from a global RNG.
        insert(
            &varmap,
            "embed.weight",
            rng.normal(vocab_cap * EMBED, 0.1),
            (vocab_cap, EMBED),
        );
        insert(
            &varmap,
            "hidden.weight",
            rng.normal(HIDDEN * input, (2.0 / input as f64).sqrt()),
            (HIDDEN, input),
        );
        insert(&varmap, "hidden.bias", vec![0.0; HIDDEN], HIDDEN);
        insert(
            &varmap,
            "out.weight",
            rng.normal(vocab_cap * HIDDEN, (1.0 / HIDDEN as f64).sqrt()),
            (vocab_cap, HIDDEN),
        );
        insert(&varmap, "out.bias", vec![0.0; vocab_cap], vocab_cap);
        Self::over(varmap, vocab_cap, rng, replayed)
    }

    /// Every weight zero: the forward pass is exact arithmetic, and only
    /// the output bias ever learns. The fixture for wire pins.
    #[cfg(test)]
    pub fn zeroed(vocab_cap: usize) -> Self {
        let varmap = VarMap::new();
        let input = CONTEXT * EMBED;
        insert(
            &varmap,
            "embed.weight",
            vec![0.0; vocab_cap * EMBED],
            (vocab_cap, EMBED),
        );
        insert(
            &varmap,
            "hidden.weight",
            vec![0.0; HIDDEN * input],
            (HIDDEN, input),
        );
        insert(&varmap, "hidden.bias", vec![0.0; HIDDEN], HIDDEN);
        insert(
            &varmap,
            "out.weight",
            vec![0.0; vocab_cap * HIDDEN],
            (vocab_cap, HIDDEN),
        );
        insert(&varmap, "out.bias", vec![0.0; vocab_cap], vocab_cap);
        Self::over(varmap, vocab_cap, XorShift::new(DEFAULT_SEED), REPLAYED)
    }

    fn over(varmap: VarMap, vocab_cap: usize, rng: XorShift, replayed: usize) -> Self {
        let vb = VarBuilder::from_varmap(&varmap, candle_core::DType::F32, &Device::Cpu);
        let embed = candle_nn::embedding(vocab_cap, EMBED, vb.pp("embed")).expect("embedding");
        let hidden = candle_nn::linear(CONTEXT * EMBED, HIDDEN, vb.pp("hidden")).expect("hidden");
        let out = candle_nn::linear(HIDDEN, vocab_cap, vb.pp("out")).expect("out");
        let optimizer = AdamW::new(
            varmap.all_vars(),
            ParamsAdamW {
                lr: LEARNING_RATE,
                ..ParamsAdamW::default()
            },
        )
        .expect("AdamW over the net's vars");
        Self {
            vocab_cap,
            varmap,
            embed,
            hidden,
            out,
            optimizer,
            replay: Replay::new(REPLAY, rng),
            replayed,
        }
    }

    /// The weights as safetensors bytes. The optimizer's moments are not
    /// in them: a restored net starts AdamW cold.
    pub fn weights(&self) -> Vec<u8> {
        let data = self.varmap.data().lock().expect("varmap lock");
        let tensors: Vec<(&String, Tensor)> = data
            .iter()
            .map(|(name, var)| (name, var.as_tensor().clone()))
            .collect();
        safetensors::serialize(tensors, None).expect("the net's tensors serialize")
    }

    /// A net with `weights` in place of its initial ones. Fails on bytes
    /// that are not safetensors, a missing tensor, or a shape that is not
    /// this net's, so a checkpoint from a different shape is refused
    /// rather than half-loaded.
    pub fn from_weights(vocab_cap: usize, seed: u64, weights: &[u8]) -> Result<Self> {
        let net = Self::new(vocab_cap, seed);
        let loaded = candle_core::safetensors::load_buffer(weights, &Device::Cpu)?;
        let data = net.varmap.data().lock().expect("varmap lock");
        for (name, var) in data.iter() {
            let tensor = loaded
                .get(name)
                .ok_or_else(|| candle_core::Error::Msg(format!("checkpoint has no {name}")))?;
            if tensor.dims() != var.dims() {
                return Err(candle_core::Error::Msg(format!(
                    "{name}: checkpoint shape {:?}, net shape {:?}",
                    tensor.dims(),
                    var.dims()
                )));
            }
            var.set(tensor)?;
        }
        drop(data);
        Ok(net)
    }

    /// The pair pushed last, for tests of what the engine keeps.
    #[cfg(test)]
    pub fn replay_newest(&self) -> Option<Pair> {
        self.replay.ring.back().copied()
    }

    #[cfg(test)]
    pub fn param_count(&self) -> usize {
        self.varmap
            .all_vars()
            .iter()
            .map(|var| var.elem_count())
            .sum()
    }

    /// The distribution over the next token after `context`, the lane's
    /// history oldest first.
    pub fn predict(&self, context: &[u16]) -> Prediction {
        let logp = self
            .log_probs(&[padded(context)])
            .and_then(|t| t.squeeze(0)?.to_vec1::<f32>())
            .expect("a forward pass over fixed shapes");
        Prediction { logp }
    }

    /// One AdamW step on `(context, actual)` and `replayed` pairs drawn
    /// from the ring; the pair then joins the ring when `keep` is set,
    /// which the engine does for every verdict but anomaly.
    pub fn learn(&mut self, context: &[u16], actual: u16, keep: bool) {
        let pair = (padded(context), actual);
        let mut batch = vec![pair];
        batch.extend(self.replay.sample(self.replayed));
        self.step(&batch)
            .expect("a training step over fixed shapes");
        if keep {
            self.replay.push(pair);
        }
    }

    fn step(&mut self, batch: &[Pair]) -> Result<()> {
        let contexts: Vec<[u16; CONTEXT]> = batch.iter().map(|&(context, _)| context).collect();
        let targets: Vec<u32> = batch.iter().map(|&(_, actual)| u32::from(actual)).collect();
        let logp = self.log_probs(&contexts)?;
        let targets = Tensor::from_vec(targets, batch.len(), &Device::Cpu)?;
        let loss = candle_nn::loss::nll(&logp, &targets)?;
        self.optimizer.backward_step(&loss)
    }

    fn log_probs(&self, contexts: &[[u16; CONTEXT]]) -> Result<Tensor> {
        let ids: Vec<u32> = contexts.iter().flatten().map(|&t| u32::from(t)).collect();
        let ids = Tensor::from_vec(ids, (contexts.len(), CONTEXT), &Device::Cpu)?;
        let x = self
            .embed
            .forward(&ids)?
            .reshape((contexts.len(), CONTEXT * EMBED))?;
        let h = self.hidden.forward(&x)?.relu()?;
        let logits = self.out.forward(&h)?;
        debug_assert_eq!(logits.dims(), [contexts.len(), self.vocab_cap]);
        candle_nn::ops::log_softmax(&logits, D::Minus1)
    }
}

/// The last `CONTEXT` tokens of `context`, left-padded with BOS.
fn padded(context: &[u16]) -> [u16; CONTEXT] {
    let mut out = [BOS; CONTEXT];
    let tail = &context[context.len().saturating_sub(CONTEXT)..];
    out[CONTEXT - tail.len()..].copy_from_slice(tail);
    out
}

fn insert<S: Into<candle_core::Shape>>(varmap: &VarMap, name: &str, data: Vec<f32>, shape: S) {
    let tensor = Tensor::from_vec(data, shape, &Device::Cpu).expect("a fresh tensor");
    let var = Var::from_tensor(&tensor).expect("a fresh var");
    varmap
        .data()
        .lock()
        .expect("varmap lock")
        .insert(name.to_string(), var);
}

/// The last `cap` pairs pushed, sampled uniformly with replacement.
pub struct Replay {
    ring: VecDeque<Pair>,
    cap: usize,
    rng: XorShift,
}

impl Replay {
    pub fn new(cap: usize, rng: XorShift) -> Self {
        Self {
            ring: VecDeque::with_capacity(cap),
            cap,
            rng,
        }
    }

    pub fn push(&mut self, pair: Pair) {
        if self.ring.len() == self.cap {
            self.ring.pop_front();
        }
        self.ring.push_back(pair);
    }

    /// `n` draws, or none from an empty ring.
    pub fn sample(&mut self, n: usize) -> Vec<Pair> {
        if self.ring.is_empty() {
            return Vec::new();
        }
        (0..n)
            .map(|_| self.ring[self.rng.below(self.ring.len())])
            .collect()
    }

    #[cfg(test)]
    pub fn len(&self) -> usize {
        self.ring.len()
    }
}

/// xorshift64: enough for sampling a ring, and seedable so a test is a
/// number rather than a distribution.
pub struct XorShift(u64);

impl XorShift {
    pub fn new(seed: u64) -> Self {
        Self(if seed == 0 {
            0x9E37_79B9_7F4A_7C15
        } else {
            seed
        })
    }

    fn next(&mut self) -> u64 {
        let mut x = self.0;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.0 = x;
        x
    }

    /// Uniform in `0..n`.
    fn below(&mut self, n: usize) -> usize {
        (self.next() % n as u64) as usize
    }

    fn unit(&mut self) -> f64 {
        (self.next() >> 11) as f64 / (1u64 << 53) as f64
    }

    /// `n` draws from N(0, std²), Box–Muller.
    fn normal(&mut self, n: usize, std: f64) -> Vec<f32> {
        (0..n)
            .map(|_| {
                let u1 = self.unit().max(1e-300);
                let u2 = self.unit();
                let z = (-2.0 * u1.ln()).sqrt() * (2.0 * std::f64::consts::PI * u2).cos();
                (z * std) as f32
            })
            .collect()
    }
}

#[cfg(test)]
pub(crate) mod fixtures {
    use super::*;

    /// Runs `steps` of a deterministic cycle over `tokens` through the net,
    /// each token predicted from the true history before it is learned,
    /// and hands back every surprise in order. `observe` is told each one.
    pub fn run_cycle(
        net: &mut Net,
        tokens: &[u16],
        steps: usize,
        learn: bool,
        mut observe: impl FnMut(f64),
    ) -> Vec<f64> {
        let mut history: Vec<u16> = Vec::new();
        let mut out = Vec::with_capacity(steps);
        for i in 0..steps {
            let actual = tokens[i % tokens.len()];
            let context = &history[history.len().saturating_sub(CONTEXT)..];
            let surprise = net.predict(context).surprise(actual);
            if learn {
                net.learn(context, actual, true);
            }
            observe(surprise);
            out.push(surprise);
            history.push(actual);
        }
        out
    }

    pub fn mean(xs: &[f64]) -> f64 {
        xs.iter().sum::<f64>() / xs.len() as f64
    }
}

#[cfg(test)]
mod tests {
    use super::{fixtures::*, *};
    use crate::score::{Scorer, Verdict, WARMUP};

    const CAP: usize = 16;
    // Period nine over an eight-token window: the context never holds a
    // whole cycle, so the two grammars share a context that they finish
    // differently, and B genuinely overwrites A where they meet.
    const GRAMMAR_A: [u16; 9] = [2, 3, 4, 5, 6, 7, 8, 9, 10];
    const GRAMMAR_B: [u16; 9] = [2, 3, 4, 5, 6, 7, 8, 9, 11];

    #[test]
    fn the_default_shape_has_the_promised_parameter_count() {
        let net = Net::new(crate::token::DEFAULT_CAP, 1);
        // 2048·16 + 64·128 + 64 + 2048·64 + 2048
        assert_eq!(net.param_count(), 174_144);
    }

    #[test]
    fn a_fresh_net_is_near_uniform_and_seeded() {
        let net = Net::new(CAP, 7);
        let p = net.predict(&[]);
        let uniform = -(CAP as f64).ln();
        for lp in &p.logp {
            assert!((f64::from(*lp) - uniform).abs() < 1.0, "{lp}");
        }
        let again = Net::new(CAP, 7).predict(&[]);
        assert_eq!(p.logp, again.logp, "the same seed is the same net");
        let other = Net::new(CAP, 8).predict(&[]);
        assert_ne!(p.logp, other.logp, "a different seed is a different net");
    }

    #[test]
    fn a_short_context_is_padded_with_bos_and_a_long_one_is_its_tail() {
        assert_eq!(padded(&[]), [BOS; CONTEXT]);
        assert_eq!(padded(&[5, 6]), [BOS, BOS, BOS, BOS, BOS, BOS, 5, 6]);
        assert_eq!(
            padded(&[1, 2, 3, 4, 5, 6, 7, 8, 9, 10]),
            [3, 4, 5, 6, 7, 8, 9, 10]
        );
    }

    #[test]
    fn top_ranks_the_first_vocab_size_tokens_without_bos() {
        let p = Prediction {
            logp: vec![-3.0, 0.0, -1.0, -0.5, -2.0, -0.1],
        };
        assert_eq!(
            p.top(5, 3)
                .into_iter()
                .map(|(id, _)| id)
                .collect::<Vec<_>>(),
            [3, 2, 4],
            "BOS has the highest log-prob and is skipped; id 5 is past the vocabulary"
        );
        let (_, p3) = p.top(5, 1)[0];
        assert!((p3 - (-0.5f32).exp() as f64).abs() < 1e-9);
        assert_eq!(p.surprise(2), 1.0);
    }

    #[test]
    fn loss_falls_on_a_seeded_synthetic_grammar() {
        let mut net = Net::new(CAP, 3);
        let surprise = run_cycle(&mut net, &GRAMMAR_A, 300, true, |_| {});
        let early = mean(&surprise[..9]);
        let late = mean(&surprise[270..]);
        assert!(early > 2.0, "a fresh net is surprised: {early}");
        assert!(late < 0.2, "a trained net is not: {late}");
        // Prediction, not just loss: the next token is the top guess.
        let top = net.predict(&[4, 5, 6, 7, 8, 9, 10, 2]).top(CAP, 1);
        assert_eq!(top[0].0, 3);
        assert!(top[0].1 > 0.8, "{}", top[0].1);
    }

    #[test]
    fn learning_does_not_happen_in_predict() {
        let mut net = Net::new(CAP, 3);
        let before = net.predict(&[2, 3]).logp.clone();
        let _ = net.predict(&[2, 3]);
        assert_eq!(net.predict(&[2, 3]).logp, before);
        net.learn(&[2, 3], 4, true);
        assert_ne!(net.predict(&[2, 3]).logp, before);
    }

    #[test]
    fn an_injected_transition_scores_above_the_nets_own_threshold() {
        let mut net = Net::new(CAP, 5);
        let mut scorer = Scorer::default();
        run_cycle(&mut net, &GRAMMAR_A, WARMUP as usize + 50, true, |s| {
            scorer.observe(s);
        });
        let threshold = scorer.threshold().expect("warmed up");
        let regular = net.predict(&[4, 5, 6, 7, 8, 9, 10, 2]);
        assert_eq!(
            scorer.judge(regular.surprise(3)),
            Verdict::Expected,
            "surprise {} mean {} sigma {}",
            regular.surprise(3),
            scorer.mean(),
            scorer.sigma()
        );
        assert_eq!(scorer.judge(regular.surprise(12)), Verdict::Anomaly);
        assert!(
            regular.surprise(12) > threshold,
            "{} <= {threshold}",
            regular.surprise(12)
        );
    }

    // Grammar A, then B for as long, then A again: with replay the ring
    // still holds A pairs while B is learned, so A is not forgotten; the
    // no-replay control is the same seed with the ring unused.
    #[test]
    fn replay_keeps_grammar_a_alive_through_grammar_b() {
        let loss_on_a_after_b = |replayed: usize| {
            let mut net = Net::with_replay(CAP, 11, replayed);
            run_cycle(&mut net, &GRAMMAR_A, 300, true, |_| {});
            let learned = mean(&run_cycle(&mut net, &GRAMMAR_A, 90, false, |_| {}));
            run_cycle(&mut net, &GRAMMAR_B, 300, true, |_| {});
            let after = mean(&run_cycle(&mut net, &GRAMMAR_A, 90, false, |_| {}));
            (learned, after)
        };
        let (learned, without) = loss_on_a_after_b(0);
        let (_, with) = loss_on_a_after_b(REPLAYED);
        assert!(learned < 0.2, "A was learned: {learned}");
        assert!(without > 1.0, "the control forgot A: {without}");
        assert!(
            with < without / 2.0,
            "with replay {with}, without {without}"
        );
    }

    #[test]
    fn the_ring_keeps_the_last_cap_pairs_and_samples_only_from_them() {
        let mut replay = Replay::new(4, XorShift::new(1));
        assert!(replay.sample(2).is_empty(), "nothing to draw from");
        replay.push(([BOS; CONTEXT], 0));
        assert_eq!(
            replay.sample(3),
            vec![([BOS; CONTEXT], 0); 3],
            "one pair is drawn as often as asked"
        );
        for actual in 1..6u16 {
            replay.push(([BOS; CONTEXT], actual));
        }
        assert_eq!(replay.len(), 4);
        let drawn: Vec<u16> = replay.sample(64).into_iter().map(|(_, a)| a).collect();
        assert_eq!(drawn.len(), 64);
        for actual in 2..6u16 {
            assert!(drawn.contains(&actual), "{actual} was never drawn");
        }
        assert!(
            drawn.iter().all(|&a| a >= 2),
            "an evicted pair was drawn: {drawn:?}"
        );
        let again: Vec<u16> = Replay::new(4, XorShift::new(1))
            .sample(1)
            .into_iter()
            .map(|(_, a)| a)
            .collect();
        assert!(again.is_empty());
    }

    #[test]
    fn the_sampler_is_a_function_of_its_seed() {
        let draws = |seed: u64| {
            let mut replay = Replay::new(8, XorShift::new(seed));
            for actual in 0..8u16 {
                replay.push(([BOS; CONTEXT], actual));
            }
            replay
                .sample(16)
                .into_iter()
                .map(|(_, a)| a)
                .collect::<Vec<_>>()
        };
        assert_eq!(draws(1), draws(1));
        assert_ne!(draws(1), draws(2));
        assert_eq!(
            XorShift::new(0).next(),
            XorShift::new(0).next(),
            "a zero seed still runs"
        );
        assert_ne!(XorShift::new(0).next(), 0);
    }

    #[test]
    fn learning_a_pair_keeps_it_only_when_told() {
        let mut net = Net::new(CAP, 1);
        net.learn(&[2], 3, false);
        assert_eq!(net.replay.len(), 0, "an anomaly stays out of the ring");
        net.learn(&[2], 3, true);
        assert_eq!(net.replay.len(), 1);
    }

    #[test]
    fn weights_round_trip_through_safetensors_and_a_wrong_shape_is_refused() {
        let mut net = Net::new(CAP, 9);
        run_cycle(&mut net, &GRAMMAR_A, 50, true, |_| {});
        let bytes = net.weights();
        let restored = Net::from_weights(CAP, 1, &bytes).unwrap();
        assert_eq!(
            restored.predict(&[2, 3, 4]).logp,
            net.predict(&[2, 3, 4]).logp,
            "the weights came back, whatever the seed"
        );
        assert!(
            Net::from_weights(CAP + 1, 1, &bytes).is_err(),
            "a different vocabulary cap"
        );
        assert!(Net::from_weights(CAP, 1, b"not safetensors").is_err());
        let partial = {
            let data = net.varmap.data().lock().unwrap();
            let one: Vec<(&String, Tensor)> = data
                .iter()
                .take(1)
                .map(|(n, v)| (n, v.as_tensor().clone()))
                .collect();
            safetensors::serialize(one, None).unwrap()
        };
        assert!(
            Net::from_weights(CAP, 1, &partial).is_err(),
            "a missing tensor"
        );
    }

    #[test]
    fn a_zeroed_net_is_exactly_uniform() {
        let net = Net::zeroed(CAP);
        let p = net.predict(&[2, 3]);
        assert_eq!(p.surprise(2), f64::from((CAP as f32).ln()));
    }
}
