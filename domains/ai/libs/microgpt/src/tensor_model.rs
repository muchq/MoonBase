use std::collections::HashMap;

use candle_core::{DType, Device, Result, Tensor, Var, D};
use candle_nn::VarMap;

use crate::model::ModelConfig;

/// A GPT model backed by candle tensors for batched forward passes and autograd.
///
/// Uses the same architecture and weight naming as `Gpt` (the scalar autograd
/// model), producing identical `weights.json` output for interoperability with
/// `InferenceGpt`.
pub struct TensorGpt {
    pub varmap: VarMap,
    pub vocab_size: usize,
    pub config: ModelConfig,
    pub device: Device,
}

/// Simple xorshift64 RNG matching the one in `model.rs`.
struct Rng {
    state: u64,
}

impl Rng {
    fn new(seed: u64) -> Self {
        Rng {
            state: if seed == 0 { 1 } else { seed },
        }
    }

    fn next_u64(&mut self) -> u64 {
        let mut x = self.state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.state = x;
        x
    }

    fn gauss(&mut self, mean: f64, std: f64) -> f64 {
        let u1 = (self.next_u64() as f64) / (u64::MAX as f64);
        let u2 = (self.next_u64() as f64) / (u64::MAX as f64);
        let u1 = u1.max(1e-15);
        let z = (-2.0 * u1.ln()).sqrt() * (2.0 * std::f64::consts::PI * u2).cos();
        mean + std * z
    }
}

fn init_tensor(rng: &mut Rng, rows: usize, cols: usize, std: f64, device: &Device) -> Tensor {
    let data: Vec<f32> = (0..rows * cols)
        .map(|_| rng.gauss(0.0, std) as f32)
        .collect();
    Tensor::from_vec(data, (rows, cols), device).unwrap()
}

/// Insert a tensor as a tracked Var into the VarMap.
fn insert_var(varmap: &VarMap, name: &str, tensor: Tensor) {
    let var = Var::from_tensor(&tensor).unwrap();
    varmap.data().lock().unwrap().insert(name.to_string(), var);
}

impl TensorGpt {
    /// Create a new randomly-initialized model on the given device.
    pub fn new(vocab_size: usize, seed: u64, config: ModelConfig, device: &Device) -> Self {
        let mut rng = Rng::new(seed);
        let std = 0.08;
        let n_embd = config.n_embd;

        let varmap = VarMap::new();

        let mut add = |name: &str, rows: usize, cols: usize| {
            let t = init_tensor(&mut rng, rows, cols, std, device);
            insert_var(&varmap, name, t);
        };

        add("wte", vocab_size, n_embd);
        add("wpe", config.block_size, n_embd);
        add("lm_head", vocab_size, n_embd);

        for i in 0..config.n_layer {
            add(&format!("layer{i}.attn_wq"), n_embd, n_embd);
            add(&format!("layer{i}.attn_wk"), n_embd, n_embd);
            add(&format!("layer{i}.attn_wv"), n_embd, n_embd);
            add(&format!("layer{i}.attn_wo"), n_embd, n_embd);
            add(&format!("layer{i}.mlp_fc1"), 4 * n_embd, n_embd);
            add(&format!("layer{i}.mlp_fc2"), n_embd, 4 * n_embd);
        }

        TensorGpt {
            varmap,
            vocab_size,
            config,
            device: device.clone(),
        }
    }

    /// Get a weight tensor by name from the VarMap.
    fn w(&self, name: &str) -> Tensor {
        self.varmap
            .data()
            .lock()
            .unwrap()
            .get(name)
            .unwrap_or_else(|| panic!("missing weight: {name}"))
            .as_tensor()
            .clone()
    }

    /// Batched forward pass: `tokens[0..seq_len]` → `[seq_len, vocab_size]` logits.
    ///
    /// Uses a causal (lower-triangular) attention mask so each position can only
    /// attend to itself and earlier positions.
    pub fn forward(&self, tokens: &[usize]) -> Result<Tensor> {
        let seq_len = tokens.len();
        let n_embd = self.config.n_embd;
        let n_head = self.config.n_head;
        let n_layer = self.config.n_layer;
        let head_dim = self.config.head_dim();

        let wte = self.w("wte");
        let wpe = self.w("wpe");

        let token_ids = Tensor::new(
            tokens.iter().map(|&t| t as u32).collect::<Vec<_>>(),
            &self.device,
        )?;
        let pos_ids = Tensor::new(
            (0..seq_len as u32).collect::<Vec<_>>(),
            &self.device,
        )?;

        let tok_emb = wte.index_select(&token_ids, 0)?;
        let pos_emb = wpe.index_select(&pos_ids, 0)?;

        let mut x = tok_emb.add(&pos_emb)?;
        x = rmsnorm(&x)?;

        let mask = build_causal_mask(seq_len, &self.device)?;

        for li in 0..n_layer {
            let x_residual = x.clone();
            x = rmsnorm(&x)?;

            let wq = self.w(&format!("layer{li}.attn_wq"));
            let wk = self.w(&format!("layer{li}.attn_wk"));
            let wv = self.w(&format!("layer{li}.attn_wv"));
            let wo = self.w(&format!("layer{li}.attn_wo"));

            // Q, K, V: [seq_len, n_embd] @ [n_embd, n_embd]^T = [seq_len, n_embd]
            let q = x.matmul(&wq.t()?)?;
            let k = x.matmul(&wk.t()?)?;
            let v = x.matmul(&wv.t()?)?;

            // Reshape to [n_head, seq_len, head_dim]
            let q = q
                .reshape((seq_len, n_head, head_dim))?
                .permute((1, 0, 2))?;
            let k = k
                .reshape((seq_len, n_head, head_dim))?
                .permute((1, 0, 2))?;
            let v = v
                .reshape((seq_len, n_head, head_dim))?
                .permute((1, 0, 2))?;

            // Attention scores: [n_head, seq_len, seq_len]
            let scale = (head_dim as f64).sqrt();
            let attn_logits = q.matmul(&k.t()?)?.affine(1.0 / scale, 0.0)?;

            let attn_logits = attn_logits.broadcast_add(&mask)?;
            let attn_weights = candle_nn::ops::softmax(&attn_logits, D::Minus1)?;

            let attn_out = attn_weights.matmul(&v)?;

            // [n_head, seq_len, head_dim] → [seq_len, n_embd]
            let attn_out = attn_out
                .permute((1, 0, 2))?
                .reshape((seq_len, n_embd))?
                .contiguous()?;

            x = attn_out.matmul(&wo.t()?)?;
            x = x.add(&x_residual)?;

            // MLP
            let x_residual = x.clone();
            x = rmsnorm(&x)?;

            let fc1 = self.w(&format!("layer{li}.mlp_fc1"));
            let fc2 = self.w(&format!("layer{li}.mlp_fc2"));

            x = x.matmul(&fc1.t()?)?;
            x = x.relu()?;
            x = x.matmul(&fc2.t()?)?;
            x = x.add(&x_residual)?;
        }

        let lm_head = self.w("lm_head");
        let logits = x.matmul(&lm_head.t()?)?;
        Ok(logits)
    }

    /// Serialize model weights to JSON (same format as `Gpt::save_weights`).
    pub fn save_weights(&self) -> String {
        let data = self.varmap.data().lock().unwrap();
        let snapshot: HashMap<String, Vec<Vec<f64>>> = data
            .iter()
            .map(|(name, var)| {
                let t = var.as_tensor();
                let mat: Vec<Vec<f64>> = if t.dtype() == DType::F32 {
                    t.to_vec2::<f32>()
                        .unwrap()
                        .into_iter()
                        .map(|row| row.into_iter().map(|v| v as f64).collect())
                        .collect()
                } else {
                    t.to_vec2::<f64>().unwrap()
                };
                (name.clone(), mat)
            })
            .collect();
        serde_json::to_string(&snapshot).expect("serialization should not fail")
    }

    /// Load model weights from JSON (same format as `Gpt::load_weights`).
    pub fn load_weights_with_config(
        vocab_size: usize,
        json: &str,
        config: ModelConfig,
        device: &Device,
    ) -> Result<Self> {
        let snapshot: HashMap<String, Vec<Vec<f64>>> =
            serde_json::from_str(json).map_err(|e| candle_core::Error::Msg(format!("{e}")))?;

        let varmap = VarMap::new();

        for (name, mat) in &snapshot {
            let rows = mat.len();
            let cols = if rows > 0 { mat[0].len() } else { 0 };
            let flat: Vec<f32> = mat.iter().flatten().map(|&v| v as f32).collect();
            let tensor = Tensor::from_vec(flat, (rows, cols), device)?;
            insert_var(&varmap, name, tensor);
        }

        Ok(TensorGpt {
            varmap,
            vocab_size,
            config,
            device: device.clone(),
        })
    }
}

/// RMS normalization along the last dimension.
fn rmsnorm(x: &Tensor) -> Result<Tensor> {
    let x_sq = x.sqr()?;
    let mean = x_sq.mean_keepdim(D::Minus1)?;
    let scale = mean
        .broadcast_add(&Tensor::new(1e-5, x.device())?.to_dtype(x.dtype())?)?
        .powf(-0.5)?;
    x.broadcast_mul(&scale)
}

/// Build a causal attention mask: 0 for allowed positions, -inf for masked.
fn build_causal_mask(seq_len: usize, device: &Device) -> Result<Tensor> {
    let mut mask_data = vec![0.0f32; seq_len * seq_len];
    for i in 0..seq_len {
        for j in (i + 1)..seq_len {
            mask_data[i * seq_len + j] = f32::NEG_INFINITY;
        }
    }
    Tensor::from_vec(mask_data, (seq_len, seq_len), device)
}
