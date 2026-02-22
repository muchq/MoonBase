use std::collections::HashMap;

use candle_core::{DType, Device, Result, Tensor, D};
use candle_nn::VarMap;
use safetensors::tensor::{Dtype as StDtype, SafeTensors, TensorView};
use tracing::{debug_span, instrument};

use crate::tensor_model::{st_view_to_f32, TensorGpt};
use crate::train::TrainConfig;

/// Custom Adam optimizer with serializable state.
///
/// Replaces candle_nn::AdamW so that m/v accumulators and step counter
/// can be checkpointed and restored for incremental training.
pub struct TensorAdam {
    vars: Vec<(String, candle_core::Var)>,
    m: Vec<Tensor>,
    v: Vec<Tensor>,
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
        let mut m = Vec::new();
        let mut v = Vec::new();

        // Sort names to ensure deterministic order if varmap uses a HashMap.
        let mut names: Vec<_> = data.keys().collect();
        names.sort();

        for name in names {
            let var = data.get(name).unwrap();
            let shape = var.as_tensor().dims();
            let zeros = Tensor::zeros(shape, DType::F32, &device)?;
            vars.push((name.clone(), var.clone()));
            m.push(zeros.clone());
            v.push(zeros);
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
    ///
    /// All parameters are concatenated into single flat tensors so the Adam
    /// math runs as ~15 vectorized GPU kernels instead of ~17 × N_params
    /// individual kernels.  On Metal this dramatically reduces dispatch overhead.
    pub fn backward_step(&mut self, loss: &Tensor) -> Result<()> {
        let grads = loss.backward()?;
        self.step_t += 1;

        // Collect participating parameters (those with gradients).
        let mut grad_flats: Vec<Tensor> = Vec::new();
        let mut param_flats: Vec<Tensor> = Vec::new();
        let mut m_flats: Vec<Tensor> = Vec::new();
        let mut v_flats: Vec<Tensor> = Vec::new();
        let mut indices: Vec<usize> = Vec::new();
        let mut shapes: Vec<Vec<usize>> = Vec::new();
        let mut counts: Vec<usize> = Vec::new();
        let mut model_dtype = DType::F32;

        for (i, (_name, var)) in self.vars.iter().enumerate() {
            let theta = var.as_tensor();
            if let Some(grad) = grads.get(theta) {
                model_dtype = theta.dtype();
                let n: usize = theta.dims().iter().product();
                grad_flats.push(grad.flatten_all()?);
                param_flats.push(theta.flatten_all()?);
                m_flats.push(self.m[i].flatten_all()?);
                v_flats.push(self.v[i].flatten_all()?);
                indices.push(i);
                shapes.push(theta.dims().to_vec());
                counts.push(n);
            }
        }

        if indices.is_empty() {
            return Ok(());
        }

        // Concatenate into single flat tensors — 4 kernel launches total.
        let all_grad = Tensor::cat(&grad_flats, 0)?.to_dtype(DType::F32)?;
        let all_param = Tensor::cat(&param_flats, 0)?;
        let all_m = Tensor::cat(&m_flats, 0)?;
        let all_v = Tensor::cat(&v_flats, 0)?;

        // Adam math — ~13 kernel launches (instead of ~15 × N_params).
        let m_new = all_m
            .affine(self.beta1, 0.)?
            .add(&all_grad.affine(1.0 - self.beta1, 0.)?)?;

        let v_new = all_v
            .affine(self.beta2, 0.)?
            .add(&all_grad.sqr()?.affine(1.0 - self.beta2, 0.)?)?;

        let bc1 = 1.0 - self.beta1.powi(self.step_t as i32);
        let bc2 = 1.0 - self.beta2.powi(self.step_t as i32);
        let m_hat = m_new.affine(1.0 / bc1, 0.)?;
        let v_hat = v_new.affine(1.0 / bc2, 0.)?;

        let denom = v_hat.sqrt()?.affine(1.0, self.eps)?;
        let update = m_hat.div(&denom)?.affine(self.lr, 0.)?;

        // Apply update: new_param = param - update (cast to model dtype).
        let update_cast = update.to_dtype(model_dtype)?;
        let new_param = all_param.sub(&update_cast)?;

        // Scatter results back into individual vars and m/v accumulators.
        // narrow + reshape are zero-copy views — no extra kernel launches.
        let mut offset = 0;
        for (idx, &i) in indices.iter().enumerate() {
            let n = counts[idx];
            let shape = shapes[idx].as_slice();
            self.vars[i]
                .1
                .set(&new_param.narrow(0, offset, n)?.reshape(shape)?.contiguous()?)?;
            self.m[i] = m_new.narrow(0, offset, n)?.reshape(shape)?.contiguous()?.detach();
            self.v[i] = v_new.narrow(0, offset, n)?.reshape(shape)?.contiguous()?.detach();
            offset += n;
        }

        Ok(())
    }

    /// Update the learning rate (for linear decay schedule).
    pub fn set_learning_rate(&mut self, lr: f64) {
        self.lr = lr;
    }

    /// Serialize first-moment (m) accumulators as safetensors.
    pub fn save_m_st(&self) -> Result<Vec<u8>> {
        let map: HashMap<String, Tensor> = self.vars.iter().zip(self.m.iter())
            .map(|((name, _), m)| (name.clone(), m.clone()))
            .collect();
        tensor_map_to_st(&map)
    }

    /// Serialize second-moment (v) accumulators as safetensors.
    pub fn save_v_st(&self) -> Result<Vec<u8>> {
        let map: HashMap<String, Tensor> = self.vars.iter().zip(self.v.iter())
            .map(|((name, _), v)| (name.clone(), v.clone()))
            .collect();
        tensor_map_to_st(&map)
    }

    /// Restore optimizer state from safetensors m/v and step counter.
    pub fn load_state_st(&mut self, m_bytes: &[u8], v_bytes: &[u8], step_t: usize) -> Result<()> {
        let m_map = st_to_tensor_map(m_bytes, &self.device)?;
        let v_map = st_to_tensor_map(v_bytes, &self.device)?;
        
        for (i, (name, _)) in self.vars.iter().enumerate() {
            if let Some(m) = m_map.get(name) {
                self.m[i] = m.clone();
            }
            if let Some(v) = v_map.get(name) {
                self.v[i] = v.clone();
            }
        }
        
        self.step_t = step_t;
        Ok(())
    }
}

/// Run a batched training step over multiple sequences.
///
/// Each entry in `batch` is a full token sequence (including the final target
/// token).  Sequences are padded to the longest one in the batch; loss is
/// computed only over real (non-padded) positions.
///
/// Returns the mean loss over all real token positions.
#[instrument(skip(model, batch, optimizer, config), fields(step = step, batch_size = batch.len()))]
pub fn tensor_train_step_batched(
    model: &TensorGpt,
    batch: &[Vec<usize>],
    optimizer: &mut TensorAdam,
    config: &TrainConfig,
    step: usize,
) -> Result<f64> {
    let _span = debug_span!("train_step").entered();
    
    let block_size = model.config.block_size;

    let lr_t = config.lr_at_step(step);
    optimizer.set_learning_rate(lr_t);

    // Slice each sequence: input = tokens[0..n], target = tokens[1..=n]
    let ns: Vec<usize> = batch
        .iter()
        .map(|tokens| block_size.min(tokens.len() - 1))
        .collect();

    let batch_inputs: Vec<&[usize]> = batch
        .iter()
        .zip(&ns)
        .map(|(tokens, &n)| &tokens[..n])
        .collect();

    // Forward: [B, seq_len, vocab_size]
    let logits = {
        let _s = debug_span!("forward").entered();
        model.forward_batch(&batch_inputs)?
    };
    let (b, seq, vocab) = logits.dims3()?;

    // Build flat target ids [B*seq] and mask [B*seq] (1.0 = real, 0.0 = pad)
    let mut flat_targets: Vec<u32> = Vec::with_capacity(b * seq);
    let mut flat_mask: Vec<f32> = Vec::with_capacity(b * seq);
    let mut real_count: usize = 0;

    for (i, tokens) in batch.iter().enumerate() {
        let n = ns[i];
        for j in 0..seq {
            if j < n {
                flat_targets.push(tokens[j + 1] as u32);
                flat_mask.push(1.0);
                real_count += 1;
            } else {
                flat_targets.push(0);
                flat_mask.push(0.0);
            }
        }
    }

    let device = &model.device;
    let (loss_val, loss) = {
        let _s = debug_span!("loss_calc").entered();
        // Upcast logits to F32 for numerically stable log_softmax (avoids
        // NaN with BF16/F16 where exp/log can overflow the narrow mantissa).
        let logits_flat = logits.reshape((b * seq, vocab))?.to_dtype(DType::F32)?;
        let targets_flat = Tensor::new(flat_targets, device)?; // [B*max_len]
        let mask_flat = Tensor::new(flat_mask, device)?; // [B*max_len], already f32
    
        // Per-token NLL: gather log-prob of the target token at each position
        let log_p = candle_nn::ops::log_softmax(&logits_flat, D::Minus1)?; // [B*max_len, vocab]
        let targets_idx = targets_flat.unsqueeze(1)?; // [B*max_len, 1]
        let log_p_target = log_p.gather(&targets_idx, 1)?; // [B*max_len, 1]
        let nll = log_p_target.squeeze(1)?.neg()?; // [B*max_len]
    
        // Zero out padded positions and average over real tokens
        let masked_nll = nll.mul(&mask_flat)?;
        if real_count == 0 {
            return Err(candle_core::Error::Msg("no real tokens in batch to train on".to_string()));
        }
        let loss = masked_nll
            .sum_all()?
            .affine(1.0 / real_count as f64, 0.0)?;
        let loss_val = loss.to_scalar::<f32>()? as f64;
        (loss_val, loss)
    };

    {
        let _s = debug_span!("backward").entered();
        optimizer.backward_step(&loss)?;
    }

    Ok(loss_val)
}

fn tensor_map_to_st(map: &HashMap<String, Tensor>) -> Result<Vec<u8>> {
    let mut buffers: Vec<(String, Vec<usize>, Vec<u8>)> = Vec::new();
    for (name, t) in map {
        let flat: Vec<f32> = t.flatten_all()?.to_dtype(DType::F32)?.to_vec1::<f32>()?;
        let bytes: Vec<u8> = flat.iter().flat_map(|v| v.to_le_bytes()).collect();
        buffers.push((name.clone(), t.dims().to_vec(), bytes));
    }
    let tensors: Vec<(String, TensorView)> = buffers
        .iter()
        .map(|(name, shape, bytes)| {
            TensorView::new(StDtype::F32, shape.clone(), bytes)
                .map(|tv| (name.clone(), tv))
                .map_err(|e| candle_core::Error::Msg(format!("TensorView: {e}")))
        })
        .collect::<Result<Vec<_>>>()?;
    safetensors::tensor::serialize(tensors, None)
        .map_err(|e| candle_core::Error::Msg(format!("serialize: {e}")))
}

fn st_to_tensor_map(bytes: &[u8], device: &Device) -> Result<HashMap<String, Tensor>> {
    let st = SafeTensors::deserialize(bytes)
        .map_err(|e| candle_core::Error::Msg(format!("safetensors: {e}")))?;
    let mut map = HashMap::new();
    for (name, view) in st.tensors() {
        let shape = view.shape().to_vec();
        let flat = st_view_to_f32(&view)?;
        let tensor = Tensor::from_vec(flat, shape.as_slice(), device)?;
        map.insert(name, tensor);
    }
    Ok(map)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::ModelConfig;
    use crate::tensor_model::TensorGpt;

    fn tiny_model() -> TensorGpt {
        let config = ModelConfig {
            n_embd: 8,
            n_head: 2,
            n_layer: 1,
            block_size: 16,
        };
        TensorGpt::new(/*vocab_size=*/ 10, /*seed=*/ 1, config, &Device::Cpu, DType::F32)
    }

    fn tiny_config(steps: usize) -> TrainConfig {
        TrainConfig {
            learning_rate: 0.01,
            num_steps: steps,
            ..TrainConfig::default()
        }
    }

    #[test]
    fn forward_batch_output_shape() {
        let model = tiny_model();
        // Two sequences of different lengths; inputs are the tokens minus the last.
        let s1: Vec<usize> = vec![0, 1, 2, 3];  // 4-token input → seq_len=4
        let s2: Vec<usize> = vec![0, 1];         // 2-token input → seq_len=2
        let batch: Vec<&[usize]> = vec![&s1, &s2];
        let logits = model.forward_batch(&batch).unwrap();
        // Expected: [2, block_size=16, vocab_size=10]
        assert_eq!(logits.dims(), &[2, 16, 10]);
    }

    #[test]
    fn forward_batch_single_shape() {
        let model = tiny_model();
        let seq: Vec<usize> = vec![3, 7, 1, 5, 2];
        let batch: Vec<&[usize]> = vec![&seq];
        let logits = model.forward_batch(&batch).unwrap();
        // Expected: [1, block_size=16, vocab_size=10]
        assert_eq!(logits.dims(), &[1, 16, 10]);
    }

    #[test]
    fn batched_step_returns_finite_loss() {
        let model = tiny_model();
        let config = tiny_config(100);
        let mut opt = TensorAdam::new(&model.varmap, &config).unwrap();

        // Full sequences: input = tokens[..n], target = tokens[1..=n]
        let batch = vec![
            vec![0usize, 1, 2, 3, 4],
            vec![5usize, 6, 7, 8],
        ];
        let loss = tensor_train_step_batched(&model, &batch, &mut opt, &config, 0).unwrap();
        assert!(loss.is_finite(), "loss should be finite, got {loss}");
    }

    #[test]
    fn batched_step_loss_decreases() {
        let model = tiny_model();
        let config = tiny_config(50);
        let mut opt = TensorAdam::new(&model.varmap, &config).unwrap();

        let batch = vec![vec![0usize, 1, 2, 3, 4, 5, 6, 7]];

        let first = tensor_train_step_batched(&model, &batch, &mut opt, &config, 0).unwrap();
        let mut last = first;
        for step in 1..50 {
            last = tensor_train_step_batched(&model, &batch, &mut opt, &config, step).unwrap();
        }
        assert!(last < first, "loss should decrease: {first:.4} → {last:.4}");
    }
}
