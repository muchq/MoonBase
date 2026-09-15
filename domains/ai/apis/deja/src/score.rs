//! Surprise against a moving baseline: an exponentially weighted mean and
//! variance of past surprise, and a threshold three deviations above it
//! once enough has been seen for the baseline to mean something.

use serde::{Deserialize, Serialize};

/// Steps before the threshold is live; until then every verdict is warmup.
pub const WARMUP: u64 = 1000;
pub const EWMA_ALPHA: f64 = 0.01;
pub const SIGMAS: f64 = 3.0;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Verdict {
    Warmup,
    Expected,
    Anomaly,
}

impl Verdict {
    pub fn as_str(self) -> &'static str {
        match self {
            Verdict::Warmup => "warmup",
            Verdict::Expected => "expected",
            Verdict::Anomaly => "anomaly",
        }
    }
}

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct Scorer {
    steps: u64,
    mean: f64,
    var: f64,
}

impl Scorer {
    pub fn steps(&self) -> u64 {
        self.steps
    }

    pub fn mean(&self) -> f64 {
        self.mean
    }

    pub fn sigma(&self) -> f64 {
        self.var.sqrt()
    }

    /// `mean + 3σ`, or nothing while warming up.
    pub fn threshold(&self) -> Option<f64> {
        (self.steps >= WARMUP).then(|| self.mean + SIGMAS * self.sigma())
    }

    pub fn judge(&self, surprise: f64) -> Verdict {
        match self.threshold() {
            None => Verdict::Warmup,
            Some(threshold) if surprise > threshold => Verdict::Anomaly,
            Some(_) => Verdict::Expected,
        }
    }

    /// Folds one surprise into the baseline. The first observation is the
    /// baseline; after that mean and variance move by `EWMA_ALPHA`.
    pub fn observe(&mut self, surprise: f64) {
        if self.steps == 0 {
            self.mean = surprise;
            self.var = 0.0;
        } else {
            let delta = surprise - self.mean;
            self.mean += EWMA_ALPHA * delta;
            self.var = (1.0 - EWMA_ALPHA) * (self.var + EWMA_ALPHA * delta * delta);
        }
        self.steps += 1;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn warmed(surprise: f64) -> Scorer {
        let mut scorer = Scorer::default();
        for _ in 0..WARMUP {
            scorer.observe(surprise);
        }
        scorer
    }

    #[test]
    fn nothing_is_an_anomaly_while_warming_up() {
        let mut scorer = Scorer::default();
        for _ in 0..WARMUP - 1 {
            scorer.observe(1.0);
        }
        assert_eq!(scorer.threshold(), None);
        assert_eq!(scorer.judge(1000.0), Verdict::Warmup);
        scorer.observe(1.0);
        assert!(scorer.threshold().is_some());
    }

    #[test]
    fn a_steady_stream_judges_itself_expected_and_a_spike_an_anomaly() {
        let mut scorer = Scorer::default();
        // Alternate two values so the variance is not zero.
        for i in 0..WARMUP {
            scorer.observe(if i % 2 == 0 { 1.0 } else { 2.0 });
        }
        assert!((scorer.mean() - 1.5).abs() < 0.1, "{}", scorer.mean());
        assert!(
            scorer.sigma() > 0.3 && scorer.sigma() < 0.7,
            "{}",
            scorer.sigma()
        );
        assert_eq!(scorer.judge(2.0), Verdict::Expected);
        assert_eq!(scorer.judge(scorer.threshold().unwrap()), Verdict::Expected);
        assert_eq!(
            scorer.judge(scorer.threshold().unwrap() + 0.01),
            Verdict::Anomaly
        );
        assert_eq!(scorer.judge(9.0), Verdict::Anomaly);
    }

    // A baseline with no spread is a threshold at the mean itself: the
    // slightest surprise above it is an anomaly, which is what a perfectly
    // regular stream should say about its first irregularity.
    #[test]
    fn a_constant_stream_has_a_threshold_at_its_mean() {
        let scorer = warmed(2.0);
        assert_eq!(scorer.threshold(), Some(2.0));
        assert_eq!(scorer.judge(2.0), Verdict::Expected);
        assert_eq!(scorer.judge(2.0001), Verdict::Anomaly);
    }

    // The threshold is three standard deviations out, not three variances:
    // the two only agree at zero and one.
    #[test]
    fn the_threshold_is_three_sigmas_above_the_mean() {
        let mut wide = Scorer::default();
        for i in 0..WARMUP {
            wide.observe(if i % 2 == 0 { 0.0 } else { 10.0 });
        }
        assert!(wide.sigma() > 4.0, "{}", wide.sigma());
        assert_eq!(wide.threshold(), Some(wide.mean() + SIGMAS * wide.sigma()));
        assert_eq!(
            wide.judge(wide.mean() + 4.0 * wide.sigma()),
            Verdict::Anomaly
        );
        let mut narrow = Scorer::default();
        for i in 0..WARMUP {
            narrow.observe(if i % 2 == 0 { 1.0 } else { 1.2 });
        }
        assert!(narrow.sigma() < 0.2, "{}", narrow.sigma());
        assert_eq!(
            narrow.threshold(),
            Some(narrow.mean() + SIGMAS * narrow.sigma())
        );
        assert_eq!(
            narrow.judge(narrow.mean() + 2.0 * narrow.sigma()),
            Verdict::Expected
        );
    }

    #[test]
    fn the_baseline_follows_a_sustained_change() {
        let mut scorer = warmed(1.0);
        for _ in 0..2000 {
            scorer.observe(5.0);
        }
        assert!((scorer.mean() - 5.0).abs() < 0.01, "{}", scorer.mean());
        assert_eq!(scorer.judge(5.0), Verdict::Expected);
    }
}
