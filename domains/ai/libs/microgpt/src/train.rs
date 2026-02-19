use serde::{Deserialize, Serialize};

/// Checkpoint state for resuming a training run.
#[derive(Serialize, Deserialize)]
pub struct TrainState {
    pub step: usize,
    pub adam_step: usize,
    pub learning_rate: f64,
    pub batch_size: usize,
    #[serde(default)]
    pub dataset_path: Option<String>,
    #[serde(default)]
    pub dataset_mode: Option<String>,
    /// Model weight dtype (e.g. "f32", "bf16"). Added in v0.4; absent in older checkpoints.
    #[serde(default)]
    pub dtype: Option<String>,
}

/// Configuration for a training run.
pub struct TrainConfig {
    pub learning_rate: f64,
    pub beta1: f64,
    pub beta2: f64,
    pub eps: f64,
    pub num_steps: usize,
    /// Number of steps to linearly ramp LR from 0 to `learning_rate`.
    /// Set to 0 to disable warmup (not recommended).
    pub warmup_steps: usize,
}

impl TrainConfig {
    /// Compute the learning rate at a given step, applying linear warmup
    /// followed by linear decay.
    pub fn lr_at_step(&self, step: usize) -> f64 {
        if self.num_steps == 0 {
            return 0.0;
        }
        let warmup = self.warmup_steps.min(self.num_steps);
        if step < warmup {
            self.learning_rate * (step + 1) as f64 / warmup as f64
        } else if warmup >= self.num_steps {
            self.learning_rate
        } else {
            let progress = (step - warmup) as f64 / (self.num_steps - warmup) as f64;
            self.learning_rate * (1.0 - progress)
        }
    }
}

impl Default for TrainConfig {
    fn default() -> Self {
        TrainConfig {
            learning_rate: 0.01,
            beta1: 0.85,
            beta2: 0.99,
            eps: 1e-8,
            num_steps: 1000,
            warmup_steps: 200,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn train_state_roundtrip() {
        let state = TrainState {
            step: 100,
            adam_step: 100,
            learning_rate: 0.001,
            batch_size: 32,
            dataset_path: Some("data.jsonl".to_string()),
            dataset_mode: Some("chat".to_string()),
            dtype: Some("bf16".to_string()),
        };
        let json = serde_json::to_string(&state).unwrap();
        let loaded: TrainState = serde_json::from_str(&json).unwrap();

        assert_eq!(loaded.step, 100);
        assert_eq!(loaded.adam_step, 100);
        assert_eq!(loaded.learning_rate, 0.001);
        assert_eq!(loaded.batch_size, 32);
        assert_eq!(loaded.dtype.as_deref(), Some("bf16"));
    }

    #[test]
    fn train_state_dtype_roundtrip_all_variants() {
        for dtype_str in &["f32", "bf16", "f16"] {
            let state = TrainState {
                step: 1,
                adam_step: 1,
                learning_rate: 0.01,
                batch_size: 8,
                dataset_path: None,
                dataset_mode: None,
                dtype: Some(dtype_str.to_string()),
            };
            let json = serde_json::to_string(&state).unwrap();
            let loaded: TrainState = serde_json::from_str(&json).unwrap();
            assert_eq!(loaded.dtype.as_deref(), Some(*dtype_str));
        }
    }

    #[test]
    fn train_state_backward_compat_without_dtype() {
        let json = r#"{"step":50,"adam_step":50,"learning_rate":0.01,"batch_size":16}"#;
        let loaded: TrainState = serde_json::from_str(json).unwrap();
        assert_eq!(loaded.step, 50);
        assert_eq!(loaded.batch_size, 16);
        assert!(loaded.dtype.is_none(), "old checkpoints without dtype should deserialize as None");
        assert!(loaded.dataset_path.is_none());
        assert!(loaded.dataset_mode.is_none());
    }

    // ---- LR schedule tests ----

    fn config_with(lr: f64, num_steps: usize, warmup_steps: usize) -> TrainConfig {
        TrainConfig {
            learning_rate: lr,
            num_steps,
            warmup_steps,
            ..TrainConfig::default()
        }
    }

    #[test]
    fn lr_warmup_starts_small() {
        let cfg = config_with(0.01, 1000, 200);
        let lr0 = cfg.lr_at_step(0);
        assert!(
            lr0 < cfg.learning_rate * 0.01,
            "step 0 LR should be much less than peak: {lr0}"
        );
        assert!(lr0 > 0.0, "step 0 LR should be positive: {lr0}");
    }

    #[test]
    fn lr_warmup_ramps_linearly() {
        let cfg = config_with(0.01, 1000, 100);
        let lr_mid = cfg.lr_at_step(49);
        let lr_end = cfg.lr_at_step(99);
        assert!(lr_mid < lr_end, "LR should increase during warmup");
        let expected_mid = 0.01 * 50.0 / 100.0;
        assert!(
            (lr_mid - expected_mid).abs() < 1e-12,
            "mid-warmup LR: {lr_mid} vs expected {expected_mid}"
        );
    }

    #[test]
    fn lr_reaches_peak_at_warmup_end() {
        let cfg = config_with(0.003, 10000, 200);
        let lr = cfg.lr_at_step(199);
        assert!(
            (lr - 0.003).abs() < 1e-12,
            "LR at last warmup step should equal peak: {lr}"
        );
    }

    #[test]
    fn lr_continuous_at_warmup_boundary() {
        let cfg = config_with(0.01, 1000, 100);
        let lr_before = cfg.lr_at_step(99);
        let lr_after = cfg.lr_at_step(100);
        assert!(
            (lr_before - lr_after).abs() < 1e-10,
            "LR should be continuous at warmup boundary: {lr_before} vs {lr_after}"
        );
    }

    #[test]
    fn lr_decays_after_warmup() {
        let cfg = config_with(0.01, 1000, 100);
        let lr_start_decay = cfg.lr_at_step(100);
        let lr_mid_decay = cfg.lr_at_step(550);
        let lr_end = cfg.lr_at_step(999);
        assert!(lr_start_decay > lr_mid_decay, "LR should decrease during decay");
        assert!(lr_mid_decay > lr_end, "LR should continue decreasing");
        assert!(
            lr_end < cfg.learning_rate * 0.01,
            "LR at last step should be near 0: {lr_end}"
        );
    }

    #[test]
    fn lr_no_warmup_is_pure_decay() {
        let cfg = config_with(0.01, 1000, 0);
        let lr0 = cfg.lr_at_step(0);
        assert!(
            (lr0 - 0.01).abs() < 1e-12,
            "without warmup, step 0 should get full LR: {lr0}"
        );
        let lr_last = cfg.lr_at_step(999);
        assert!(
            lr_last < cfg.learning_rate * 0.002,
            "last step should be near 0: {lr_last}"
        );
    }

    #[test]
    fn lr_warmup_equals_num_steps() {
        let cfg = config_with(0.01, 100, 100);
        let lr0 = cfg.lr_at_step(0);
        assert!(
            (lr0 - 0.01 / 100.0).abs() < 1e-12,
            "step 0: {lr0}"
        );
        let lr_last = cfg.lr_at_step(99);
        assert!(
            (lr_last - 0.01).abs() < 1e-12,
            "last step should hit peak: {lr_last}"
        );
    }

    #[test]
    fn lr_warmup_exceeds_num_steps() {
        let cfg = config_with(0.01, 100, 500);
        for step in 0..100 {
            let lr = cfg.lr_at_step(step);
            assert!(lr > 0.0, "LR should always be positive: step {step}");
            assert!(lr <= 0.01, "LR should not exceed peak: step {step}, lr={lr}");
        }
    }

    #[test]
    fn lr_zero_num_steps() {
        let cfg = config_with(0.01, 0, 100);
        assert_eq!(cfg.lr_at_step(0), 0.0);
    }

    #[test]
    fn lr_monotonic_during_warmup() {
        let cfg = config_with(0.003, 20000, 200);
        let mut prev = 0.0;
        for step in 0..200 {
            let lr = cfg.lr_at_step(step);
            assert!(lr >= prev, "warmup should be monotonically increasing at step {step}");
            prev = lr;
        }
    }

    #[test]
    fn lr_monotonic_during_decay() {
        let cfg = config_with(0.003, 20000, 200);
        let mut prev = cfg.lr_at_step(200);
        for step in 201..20000 {
            let lr = cfg.lr_at_step(step);
            assert!(lr <= prev, "decay should be monotonically decreasing at step {step}");
            prev = lr;
        }
    }

    #[test]
    fn lr_default_has_warmup() {
        let cfg = TrainConfig::default();
        assert_eq!(cfg.warmup_steps, 200);
        let lr0 = cfg.lr_at_step(0);
        assert!(lr0 < cfg.learning_rate);
    }
}
