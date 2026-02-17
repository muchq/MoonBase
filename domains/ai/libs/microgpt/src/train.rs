use serde::{Deserialize, Serialize};

/// Checkpoint state for resuming a training run.
#[derive(Serialize, Deserialize)]
pub struct TrainState {
    pub step: usize,
    pub adam_step: usize,
}

/// Configuration for a training run.
pub struct TrainConfig {
    pub learning_rate: f64,
    pub beta1: f64,
    pub beta2: f64,
    pub eps: f64,
    pub num_steps: usize,
}

impl Default for TrainConfig {
    fn default() -> Self {
        TrainConfig {
            learning_rate: 0.01,
            beta1: 0.85,
            beta2: 0.99,
            eps: 1e-8,
            num_steps: 1000,
        }
    }
}
