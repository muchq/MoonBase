use std::collections::HashMap;

use candle_core::{DType, Device, Result, Tensor};
use candle_nn::VarMap;
use safetensors::tensor::{Dtype as StDtype, SafeTensors, TensorView};

use crate::tensor_model::{st_view_to_f32, TensorGpt};
use crate::train::TrainConfig;

/// Custom Adam optimizer with serializable state.
///
/// Replaces candle_nn::AdamW so that m/v accumulators and step counter
/// can be checkpointed and restored for incremental training.
pub struct TensorAdam {
    vars: Vec<(String, candle_core::Var)>,
    m: HashMap<String, Tensor>,
    v: HashMap<String, Tensor>,
    pub step_t: usize,
    lr: f64,
    beta1: f64,
    beta2: f64,
    eps: f64,
    device: Device,
}

impl TensorAdam {
    pub fn new(varmap: &VarMap, config: &TrainConfig) -> Result<Self> {
        let data = varmap.data().lock().unwrap();
        let device = data
            .values()
            .next()
            .map(|v| v.as_tensor().device().clone())
            .unwrap_or(Device::Cpu);

        let mut vars = Vec::new();
        let mut m = HashMap::new();
        let mut v = HashMap::new();

        for (name, var) in data.iter() {
            let shape = var.as_tensor().dims();
            let zeros = Tensor::zeros(shape, DType::F32, &device)?;
            vars.push((name.clone(), var.clone()));
            m.insert(name.clone(), zeros.clone());
            v.insert(name.clone(), zeros);
        }

        Ok(TensorAdam {
            vars,
            m,
            v,
            step_t: 0,
            lr: config.learning_rate,
            beta1: config.beta1,
            beta2: config.beta2,
            eps: config.eps,
            device,
        })
    }

    /// Compute gradients and apply one Adam update step.
    pub fn backward_step(&mut self, loss: &Tensor) -> Result<()> {
        let grads = loss.backward()?;
        self.step_t += 1;

        for (name, var) in &self.vars {
            let theta = var.as_tensor();
            let grad = match grads.get(theta) {
                Some(grad) => grad,
                None => continue,
            };

            let m_prev = self.m.get(name).unwrap();
            let v_prev = self.v.get(name).unwrap();

            // m_t = beta1 * m_{t-1} + (1 - beta1) * grad
            let m_new = m_prev
                .affine(self.beta1, 0.)?
                .add(&grad.affine(1.0 - self.beta1, 0.)?)?;

            // v_t = beta2 * v_{t-1} + (1 - beta2) * grad^2
            let v_new = v_prev
                .affine(self.beta2, 0.)?
                .add(&grad.sqr()?.affine(1.0 - self.beta2, 0.)?)?;

            // Bias correction
            let bc1 = 1.0 - self.beta1.powi(self.step_t as i32);
            let bc2 = 1.0 - self.beta2.powi(self.step_t as i32);
            let m_hat = m_new.affine(1.0 / bc1, 0.)?;
            let v_hat = v_new.affine(1.0 / bc2, 0.)?;

            // param -= lr * m_hat / (sqrt(v_hat) + eps)
            let denom = v_hat.sqrt()?.affine(1.0, self.eps)?;
            let update = m_hat.div(&denom)?.affine(self.lr, 0.)?;
            var.set(&theta.sub(&update)?)?;

            self.m.insert(name.clone(), m_new);
            self.v.insert(name.clone(), v_new);
        }

        Ok(())
    }

    /// Update the learning rate (for linear decay schedule).
    pub fn set_learning_rate(&mut self, lr: f64) {
        self.lr = lr;
    }

    /// Serialize first-moment (m) accumulators as safetensors.
    pub fn save_m_st(&self) -> Vec<u8> {
        tensor_map_to_st(&self.m)
    }

    /// Serialize second-moment (v) accumulators as safetensors.
    pub fn save_v_st(&self) -> Vec<u8> {
        tensor_map_to_st(&self.v)
    }

    /// Restore optimizer state from safetensors m/v and step counter.
    pub fn load_state_st(&mut self, m_bytes: &[u8], v_bytes: &[u8], step_t: usize) -> Result<()> {
        self.m = st_to_tensor_map(m_bytes, &self.device)?;
        self.v = st_to_tensor_map(v_bytes, &self.device)?;
        self.step_t = step_t;
        Ok(())
    }

}

fn tensor_map_to_st(map: &HashMap<String, Tensor>) -> Vec<u8> {
    let buffers: Vec<(String, Vec<usize>, Vec<u8>)> = map
        .iter()
        .map(|(name, t)| {
            let flat: Vec<f32> = t.flatten_all().unwrap().to_vec1::<f32>().unwrap();
            let bytes: Vec<u8> = flat.iter().flat_map(|v| v.to_le_bytes()).collect();
            (name.clone(), t.dims().to_vec(), bytes)
        })
        .collect();
    let tensors: Vec<(String, TensorView)> = buffers
        .iter()
        .map(|(name, shape, bytes)| {
            (
                name.clone(),
                TensorView::new(StDtype::F32, shape.clone(), bytes).unwrap(),
            )
        })
        .collect();
    safetensors::tensor::serialize(tensors, None).unwrap()
}

fn st_to_tensor_map(bytes: &[u8], device: &Device) -> Result<HashMap<String, Tensor>> {
    let st = SafeTensors::deserialize(bytes)
        .map_err(|e| candle_core::Error::Msg(format!("safetensors: {e}")))?;
    let mut map = HashMap::new();
    for (name, view) in st.tensors() {
        let shape = view.shape().to_vec();
        let flat = st_view_to_f32(&view);
        let tensor = Tensor::from_vec(flat, shape.as_slice(), device)?;
        map.insert(name, tensor);
    }
    Ok(map)
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
    optimizer.backward_step(&loss)?;

    Ok(loss_val)
}
