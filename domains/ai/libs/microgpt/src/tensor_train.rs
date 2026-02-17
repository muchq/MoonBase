use candle_core::{Result, Tensor};
use candle_nn::optim::Optimizer;

use crate::tensor_model::TensorGpt;
use crate::train::TrainConfig;

/// Adam optimizer wrapping candle's AdamW with weight_decay=0.
pub struct TensorAdam {
    inner: candle_nn::AdamW,
}

impl TensorAdam {
    pub fn new(varmap: &candle_nn::VarMap, config: &TrainConfig) -> Result<Self> {
        let params = candle_nn::ParamsAdamW {
            lr: config.learning_rate,
            beta1: config.beta1,
            beta2: config.beta2,
            eps: config.eps,
            weight_decay: 0.0,
        };
        let inner = candle_nn::AdamW::new(varmap.all_vars(), params)?;
        Ok(TensorAdam { inner })
    }

    /// Update the learning rate (for linear decay schedule).
    pub fn set_learning_rate(&mut self, lr: f64) {
        self.inner.set_learning_rate(lr);
    }
}

/// Run a single training step. Returns the scalar loss value.
///
/// Slices `tokens` to at most `block_size` input positions, runs the batched
/// forward pass, computes cross-entropy loss against the next-token targets,
/// and updates parameters via Adam.
pub fn tensor_train_step(
    model: &TensorGpt,
    tokens: &[usize],
    optimizer: &mut TensorAdam,
    config: &TrainConfig,
    step: usize,
) -> Result<f64> {
    let block_size = model.config.block_size;
    let n = block_size.min(tokens.len() - 1);

    // Linear LR decay
    let lr_t = config.learning_rate * (1.0 - step as f64 / config.num_steps as f64);
    optimizer.set_learning_rate(lr_t);

    // Input tokens: positions 0..n, targets: positions 1..n+1
    let input_tokens = &tokens[..n];
    let target_tokens: Vec<u32> = tokens[1..=n].iter().map(|&t| t as u32).collect();

    // Forward: [n, vocab_size]
    let logits = model.forward(input_tokens)?;

    // Cross-entropy loss
    let targets = Tensor::new(target_tokens, &model.device)?;
    let loss = candle_nn::loss::cross_entropy(&logits, &targets)?;
    let loss_val = loss.to_scalar::<f32>()? as f64;

    // Backward + update
    optimizer.inner.backward_step(&loss)?;

    Ok(loss_val)
}
