use std::collections::HashMap;

use candle_core::{DType, Device, Result, Tensor, Var};
use candle_nn::{loss, ops};

use crate::model::ModelConfig;
use crate::tensor::{tensor_to_vec2, vec2_to_tensor};

/// A GPT model with matrix-valued weights for batched training using Candle.
pub struct TensorGpt {
    pub state_dict: HashMap<String, Var>,
    pub config: ModelConfig,
    pub device: Device,
}

impl TensorGpt {
    pub fn new(vocab_size: usize, seed: u64) -> Result<Self> {
        Self::with_config(vocab_size, seed, ModelConfig::default())
    }

    pub fn with_config(vocab_size: usize, seed: u64, config: ModelConfig) -> Result<Self> {
        // Use CPU for now.
        let device = Device::Cpu;
        let std = 0.08;
        let n_embd = config.n_embd;
        let mut sd = HashMap::new();

        let mut rng = crate::model::Rng::new(seed);

        let mut init = |rows, cols| -> Result<Var> {
            let data: Vec<f64> = (0..rows * cols)
                .map(|_| rng.gauss(0.0, std))
                .collect();
            let t = Tensor::from_vec(data, (rows, cols), &device)?;
            Ok(Var::from_tensor(&t)?)
        };

        sd.insert("wte".into(), init(vocab_size, n_embd)?);
        sd.insert("wpe".into(), init(config.block_size, n_embd)?);
        sd.insert("lm_head".into(), init(vocab_size, n_embd)?);

        for i in 0..config.n_layer {
            sd.insert(format!("layer{i}.attn_wq"), init(n_embd, n_embd)?);
            sd.insert(format!("layer{i}.attn_wk"), init(n_embd, n_embd)?);
            sd.insert(format!("layer{i}.attn_wv"), init(n_embd, n_embd)?);
            sd.insert(format!("layer{i}.attn_wo"), init(n_embd, n_embd)?);
            sd.insert(format!("layer{i}.mlp_fc1"), init(4 * n_embd, n_embd)?);
            sd.insert(format!("layer{i}.mlp_fc2"), init(n_embd, 4 * n_embd)?);
        }

        Ok(TensorGpt {
            state_dict: sd,
            config,
            device,
        })
    }

    /// Load weights from JSON string (compatible with Gpt).
    pub fn load_weights(_vocab_size: usize, json: &str) -> Result<Self> {
        let snapshot: HashMap<String, Vec<Vec<f64>>> =
            serde_json::from_str(json).map_err(|e| candle_core::Error::Msg(e.to_string()))?;

        // Infer config from shapes? Or use default?
        // The original load_weights uses default config.
        let config = ModelConfig::default(); // simplistic
        // Better to require config or infer it.
        // Let's stick to default for now, matching `Gpt::load_weights`.

        let device = Device::Cpu;
        let mut sd = HashMap::new();

        for (k, v) in snapshot {
            let t = vec2_to_tensor(&v, &device)?;
            sd.insert(k, Var::from_tensor(&t)?);
        }

        Ok(TensorGpt {
            state_dict: sd,
            config,
            device,
        })
    }

    pub fn save_weights(&self) -> Result<String> {
        let mut snapshot: HashMap<String, Vec<Vec<f64>>> = HashMap::new();
        for (k, v) in &self.state_dict {
            snapshot.insert(k.clone(), tensor_to_vec2(v.as_tensor())?);
        }
        serde_json::to_string(&snapshot).map_err(|e| candle_core::Error::Msg(e.to_string()))
    }

    pub fn params(&self) -> Vec<Var> {
        // Return all vars. Order doesn't strictly matter for Adam as long as consistent?
        // Actually, Adam needs consistent ordering to match momentum states.
        // So we should sort by key.
        let mut keys: Vec<&String> = self.state_dict.keys().collect();
        keys.sort();
        keys.iter().map(|k| self.state_dict[*k].clone()).collect()
    }

    /// Forward pass.
    /// input: (B, T) or (T) tensor of token indices.
    /// targets: (B, T) or (T) tensor of target indices (optional).
    pub fn forward(&self, input: &Tensor, targets: Option<&Tensor>) -> Result<(Option<Tensor>, Tensor)> {
        let (b_sz, t_sz) = input.dims2()?;
        let n_embd = self.config.n_embd;
        let n_head = self.config.n_head;
        let head_dim = self.config.head_dim();

        // 1. Embeddings
        let wte = &self.state_dict["wte"]; // (V, E)
        let wpe = &self.state_dict["wpe"]; // (MaxT, E)

        // input is (B, T). Flatten to 1D for embedding lookup, then reshape.
        let input_flat = input.reshape(b_sz * t_sz)?;
        let tok_emb = wte.as_tensor().embedding(&input_flat)?.reshape((b_sz, t_sz, n_embd))?;

        // Positional embeddings
        // Create range [0..t_sz]
        let pos_ids = Tensor::arange(0u32, t_sz as u32, &self.device)?; // (T)
        let pos_emb = wpe.as_tensor().embedding(&pos_ids)?; // (T, E)

        // Add (broadcast pos_emb to B)
        let mut x = tok_emb.broadcast_add(&pos_emb)?; // (B, T, E)

        // Helper for RMSNorm (no learned params)
        fn rmsnorm(x: &Tensor) -> Result<Tensor> {
            let eps = 1e-5;
            let x_dtype = x.dtype();
            let x = x.to_dtype(DType::F64)?; // Ensure f64 precision for norm
            let square = x.sqr()?;
            let mean_sq = square.mean_keepdim(2)?; // (B, T, 1)
            let scale = (mean_sq + eps)?.sqrt()?.recip()?;
            let x_norm = x.broadcast_mul(&scale)?;
            x_norm.to_dtype(x_dtype)
        }

        // Helper for linear layer with flattening (handles (B, T, E) input)
        let linear = |x: &Tensor, w: &Var| -> Result<Tensor> {
             let (b, t, e) = x.dims3()?;
             let x_flat = x.reshape((b * t, e))?;
             let w_t = w.t()?;
             let out_flat = x_flat.matmul(&w_t)?;
             let out_dim = w_t.dims()[1];
             out_flat.reshape((b, t, out_dim))
        };

        x = rmsnorm(&x)?; // Initial norm

        for i in 0..self.config.n_layer {
            let residual = x.clone();
            x = rmsnorm(&x)?;

            // Attention
            // Linear projections
            let wq = &self.state_dict[&format!("layer{i}.attn_wq")];
            let wk = &self.state_dict[&format!("layer{i}.attn_wk")];
            let wv = &self.state_dict[&format!("layer{i}.attn_wv")];
            let wo = &self.state_dict[&format!("layer{i}.attn_wo")];

            let q = linear(&x, wq)?;
            let k = linear(&x, wk)?;
            let v = linear(&x, wv)?;

            // Split heads: (B, T, E) -> (B, T, H, D) -> (B, H, T, D)
            // reshape first
            let q = q.reshape((b_sz, t_sz, n_head, head_dim))?.transpose(1, 2)?;
            let k = k.reshape((b_sz, t_sz, n_head, head_dim))?.transpose(1, 2)?;
            let v = v.reshape((b_sz, t_sz, n_head, head_dim))?.transpose(1, 2)?;

            // Attn scores: q @ k.t()
            // (B, H, T, D) @ (B, H, D, T) -> (B, H, T, T)
            let scale = (head_dim as f64).sqrt();
            let attn_logits = (q.matmul(&k.t()?)? / scale)?;

            // Causal mask
            // We need a mask of shape (T, T) with -inf where col > row.
            // i >= j (row >= col) -> allowed.
            let rows = Tensor::arange(0u32, t_sz as u32, &self.device)?.reshape((t_sz, 1))?;
            let cols = Tensor::arange(0u32, t_sz as u32, &self.device)?.reshape((1, t_sz))?;
            let mask = rows.broadcast_ge(&cols)?; // (T, T) boolean/u8 (1 where i >= j)

            let mask = mask.to_dtype(DType::F64)?; // (T, T) 1.0 where allowed, 0.0 where blocked
            // We want: if 1 keep, if 0 replace with -inf.
            // mask is 1 for allowed.
            // log(mask) -> 0 for 1, -inf for 0?
            // Or simpler: (1 - mask) * -1e9
            let mask = (mask.broadcast_sub(&Tensor::new(1.0f64, &self.device)?)? * -1e9f64)?;

            // Broadcast mask to (B, H, T, T)
            let attn_logits = attn_logits.broadcast_add(&mask)?;
            let attn_weights = ops::softmax(&attn_logits, 3)?;

            // Weighted sum
            // (B, H, T, T) @ (B, H, T, D) -> (B, H, T, D)
            let out = attn_weights.matmul(&v)?;

            // Merge heads
            // (B, H, T, D) -> (B, T, H, D) -> (B, T, E)
            let out = out.transpose(1, 2)?.reshape((b_sz, t_sz, n_embd))?;

            // Output projection
            let out = linear(&out, wo)?;

            x = (out + residual)?;

            // MLP
            let residual = x.clone();
            x = rmsnorm(&x)?;

            let fc1 = &self.state_dict[&format!("layer{i}.mlp_fc1")];
            let fc2 = &self.state_dict[&format!("layer{i}.mlp_fc2")];

            // x @ fc1.t() -> relu -> x @ fc2.t()
            let h = linear(&x, fc1)?;
            let h = h.relu()?;
            let out = linear(&h, fc2)?;

            x = (out + residual)?;
        }

        // Final logits
        // No final norm in this model variant
        let lm_head = &self.state_dict["lm_head"];
        let logits = linear(&x, lm_head)?; // (B, T, V)

        let loss = if let Some(t) = targets {
            // Flatten logits and targets to (B*T, V) and (B*T)
            let vocab_size = self.state_dict["wte"].dims()[0];
            let logits_flat = logits.reshape((b_sz * t_sz, vocab_size))?;
            let targets_flat = t.reshape(b_sz * t_sz)?;

            // Cross entropy
            let l = loss::cross_entropy(&logits_flat, &targets_flat)?;
            Some(l)
        } else {
            None
        };

        Ok((loss, logits))
    }
}
