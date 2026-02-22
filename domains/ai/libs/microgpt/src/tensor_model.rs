use std::collections::HashMap;

use candle_core::{Device, IndexOp, Result, Tensor, Var, D, DType};
use candle_nn::VarMap;
use safetensors::tensor::{Dtype as StDtype, SafeTensors, TensorView};

use crate::model::ModelConfig;

/// Pre-resolved weight handles for a single transformer layer.
struct LayerWeights {
    attn_wq: Var,
    attn_wk: Var,
    attn_wv: Var,
    attn_wo: Var,
    mlp_fc1: Var,
    mlp_fc2: Var,
}

/// Pre-resolved weight handles for the full model, avoiding mutex/HashMap
/// lookups on every forward pass.
struct ResolvedWeights {
    wte: Var,
    wpe: Var,
    lm_head: Var,
    layers: Vec<LayerWeights>,
}

/// A GPT model backed by candle tensors for batched forward passes and autograd.
///
/// Uses the same architecture and weight naming as `Gpt` (the scalar autograd
/// model), producing identical `weights.safetensors` output for interoperability with
/// `InferenceGpt`.
pub struct TensorGpt {
    pub varmap: VarMap,
    weights: ResolvedWeights,
    pub vocab_size: usize,
    pub config: ModelConfig,
    pub device: Device,
    pub dtype: DType,
    /// Precomputed causal attention mask [block_size, block_size] in model dtype.
    /// Cached to avoid rebuilding + CPU→GPU transfer on every forward pass.
    causal_mask: Tensor,
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

fn init_tensor(rng: &mut Rng, rows: usize, cols: usize, std: f64, device: &Device, dtype: DType) -> Tensor {
    let data: Vec<f32> = (0..rows * cols)
        .map(|_| rng.gauss(0.0, std) as f32)
        .collect();
    Tensor::from_vec(data, (rows, cols), device).unwrap().to_dtype(dtype).unwrap()
}

/// Insert a tensor as a tracked Var into the VarMap.
fn insert_var(varmap: &VarMap, name: &str, tensor: Tensor) {
    let var = Var::from_tensor(&tensor).unwrap();
    varmap.data().lock().unwrap().insert(name.to_string(), var);
}

fn resolve_weights(varmap: &VarMap, config: &ModelConfig) -> ResolvedWeights {
    let data = varmap.data().lock().unwrap();
    let get = |name: &str| -> Var {
        data.get(name)
            .unwrap_or_else(|| panic!("missing weight: {name}"))
            .clone()
    };
    let wte = get("wte");
    let wpe = get("wpe");
    let lm_head = get("lm_head");
    let layers = (0..config.n_layer)
        .map(|i| LayerWeights {
            attn_wq: get(&format!("layer{i}.attn_wq")),
            attn_wk: get(&format!("layer{i}.attn_wk")),
            attn_wv: get(&format!("layer{i}.attn_wv")),
            attn_wo: get(&format!("layer{i}.attn_wo")),
            mlp_fc1: get(&format!("layer{i}.mlp_fc1")),
            mlp_fc2: get(&format!("layer{i}.mlp_fc2")),
        })
        .collect();
    ResolvedWeights { wte, wpe, lm_head, layers }
}

impl TensorGpt {
    /// Create a new randomly-initialized model on the given device.
    pub fn new(vocab_size: usize, seed: u64, config: ModelConfig, device: &Device, dtype: DType) -> Self {
        let mut rng = Rng::new(seed);
        let std = 0.08;
        // Output projections (attn_wo, mlp_fc2) add directly to the residual stream
        // at every layer. Without scaling, residual variance grows linearly with depth,
        // producing high-variance logits and initial loss well above ln(vocab_size).
        // GPT-2 scales these by 1/sqrt(2 * n_layer) to keep the residual bounded.
        let out_std = std / (2.0 * config.n_layer as f64).sqrt();
        let n_embd = config.n_embd;

        let varmap = VarMap::new();

        let mut add = |name: &str, rows: usize, cols: usize, s: f64| {
            let t = init_tensor(&mut rng, rows, cols, s, device, dtype);
            insert_var(&varmap, name, t);
        };

        add("wte", vocab_size, n_embd, std);
        add("wpe", config.block_size, n_embd, std);
        add("lm_head", vocab_size, n_embd, std);

        for i in 0..config.n_layer {
            add(&format!("layer{i}.attn_wq"), n_embd, n_embd, std);
            add(&format!("layer{i}.attn_wk"), n_embd, n_embd, std);
            add(&format!("layer{i}.attn_wv"), n_embd, n_embd, std);
            add(&format!("layer{i}.attn_wo"), n_embd, n_embd, out_std);
            add(&format!("layer{i}.mlp_fc1"), 4 * n_embd, n_embd, std);
            add(&format!("layer{i}.mlp_fc2"), n_embd, 4 * n_embd, out_std);
        }

        let causal_mask = build_causal_mask(config.block_size, device)
            .unwrap()
            .to_dtype(dtype)
            .unwrap();

        let weights = resolve_weights(&varmap, &config);

        TensorGpt {
            varmap,
            weights,
            vocab_size,
            config,
            device: device.clone(),
            dtype,
            causal_mask,
        }
    }

    /// Batched forward pass over multiple sequences.
    ///
    /// `batch_inputs` is a slice of input token sequences (variable length; each
    /// already truncated to at most `block_size`).  Shorter sequences are padded
    /// with token 0 on the right — padding positions are ignored by the caller
    /// when computing the loss.
    ///
    /// Returns `[batch, max_seq_len, vocab_size]` logits.
    pub fn forward_batch(&self, batch_inputs: &[&[usize]]) -> Result<Tensor> {
        let batch_size = batch_inputs.len();
        assert!(batch_size > 0, "batch must be non-empty");

        // Pad to the longest sequence in the batch, rounded up to the next
        // multiple of 32 (capped at block_size).  This keeps the number of
        // distinct tensor shapes small (~8 buckets) to avoid Metal allocator
        // fragmentation, while dramatically reducing wasted compute when
        // sequences are shorter than block_size.
        let longest = batch_inputs.iter().map(|s| s.len()).max().unwrap_or(1);
        let max_len = ((longest + 31) & !31).min(self.config.block_size).max(1);

        let n_embd = self.config.n_embd;
        let n_head = self.config.n_head;
        let n_layer = self.config.n_layer;
        let head_dim = self.config.head_dim();

        let wte = self.weights.wte.as_tensor().clone();
        let wpe = self.weights.wpe.as_tensor().clone();

        // Flatten token ids: [B * max_len], pad with 0
        let mut flat_tokens: Vec<u32> = Vec::with_capacity(batch_size * max_len);
        for seq in batch_inputs {
            let take = seq.len().min(max_len);
            for &t in &seq[..take] {
                flat_tokens.push(t as u32);
            }
            for _ in take..max_len {
                flat_tokens.push(0);
            }
        }

        let flat_token_ids = Tensor::new(flat_tokens, &self.device)?; // [B*max_len]
        let tok_emb = wte.index_select(&flat_token_ids, 0)?; // [B*max_len, n_embd]
        let tok_emb = tok_emb.reshape((batch_size, max_len, n_embd))?; // [B, max_len, n_embd]

        let pos_ids = Tensor::new(
            (0..max_len as u32).collect::<Vec<_>>(),
            &self.device,
        )?;
        let pos_emb = wpe.index_select(&pos_ids, 0)?; // [max_len, n_embd]

        let mut x = tok_emb.broadcast_add(&pos_emb)?; // [B, max_len, n_embd]
        x = rmsnorm(&x)?;

        // Slice the precomputed causal mask to the current padded length.
        let mask = self.causal_mask.i((..max_len, ..max_len))?;

        // Candle's matmul does not broadcast 2D weights over a leading batch dim.
        // proj flattens [B, seq, d_in] → [B*seq, d_in], multiplies by w^T [d_in, d_out],
        // then reshapes back to [B, seq, d_out].
        let proj = |x: &Tensor, w: &Tensor| -> Result<Tensor> {
            let (b, s, d_in) = x.dims3()?;
            let d_out = w.dim(0)?;
            x.reshape((b * s, d_in))?.matmul(&w.t()?)?.reshape((b, s, d_out))
        };

        for li in 0..n_layer {
            let x_residual = x.clone();
            x = rmsnorm(&x)?;

            let lw = &self.weights.layers[li];
            let wq = lw.attn_wq.as_tensor().clone();
            let wk = lw.attn_wk.as_tensor().clone();
            let wv = lw.attn_wv.as_tensor().clone();
            let wo = lw.attn_wo.as_tensor().clone();

            // Q, K, V: [B, max_len, n_embd]
            let q = proj(&x, &wq)?;
            let k = proj(&x, &wk)?;
            let v = proj(&x, &wv)?;

            // Reshape to [B, n_head, max_len, head_dim]
            // contiguous() is needed after permute for Metal's GEMM kernel,
            // which only supports row-major or column-major strides.
            let q = q
                .reshape((batch_size, max_len, n_head, head_dim))?
                .permute((0, 2, 1, 3))?
                .contiguous()?;
            let k = k
                .reshape((batch_size, max_len, n_head, head_dim))?
                .permute((0, 2, 1, 3))?
                .contiguous()?;
            let v = v
                .reshape((batch_size, max_len, n_head, head_dim))?
                .permute((0, 2, 1, 3))?
                .contiguous()?;

            // Attention scores: [B, n_head, max_len, max_len]
            let scale = (head_dim as f64).sqrt();
            let attn_logits = q.matmul(&k.t()?)?.affine(1.0 / scale, 0.0)?;

            let attn_logits = attn_logits.broadcast_add(&mask)?;
            let attn_weights = candle_nn::ops::softmax(&attn_logits, D::Minus1)?;

            let attn_out = attn_weights.matmul(&v)?; // [B, n_head, max_len, head_dim]

            // [B, n_head, max_len, head_dim] → [B, max_len, n_embd]
            let attn_out = attn_out
                .permute((0, 2, 1, 3))?
                .reshape((batch_size, max_len, n_embd))?
                .contiguous()?;

            x = proj(&attn_out, &wo)?;
            x = x.add(&x_residual)?;

            // MLP
            let x_residual = x.clone();
            x = rmsnorm(&x)?;

            let fc1 = lw.mlp_fc1.as_tensor().clone();
            let fc2 = lw.mlp_fc2.as_tensor().clone();

            x = proj(&x, &fc1)?;
            x = x.relu()?;
            x = proj(&x, &fc2)?;
            x = x.add(&x_residual)?;
        }

        let lm_head = self.weights.lm_head.as_tensor().clone();
        let logits = proj(&x, &lm_head)?; // [B, max_len, vocab_size]
        Ok(logits)
    }

    /// Serialize model weights to safetensors, preserving the native dtype.
    pub fn save_weights_st(&self) -> Result<Vec<u8>> {
        let st_dtype = candle_dtype_to_st(self.dtype)?;
        let data = self.varmap.data().lock().unwrap();
        let mut buffers: Vec<(String, Vec<usize>, Vec<u8>)> = Vec::new();
        for (name, var) in data.iter() {
            let t = var.as_tensor();
            let bytes = tensor_to_bytes(t, self.dtype)?;
            buffers.push((name.clone(), t.dims().to_vec(), bytes));
        }
        let tensors: Vec<(String, TensorView)> = buffers
            .iter()
            .map(|(name, shape, bytes)| {
                TensorView::new(st_dtype, shape.clone(), bytes)
                    .map(|tv| (name.clone(), tv))
                    .map_err(|e| candle_core::Error::Msg(format!("TensorView: {e}")))
            })
            .collect::<Result<Vec<_>>>()?;
        safetensors::tensor::serialize(tensors, None)
            .map_err(|e| candle_core::Error::Msg(format!("serialize: {e}")))
    }

    /// Load model weights from safetensors bytes.
    pub fn load_weights_st(
        vocab_size: usize,
        bytes: &[u8],
        config: ModelConfig,
        device: &Device,
    ) -> Result<Self> {
        let st = SafeTensors::deserialize(bytes)
            .map_err(|e| candle_core::Error::Msg(format!("safetensors: {e}")))?;
        let varmap = VarMap::new();
        
        let first_st_dtype = st.tensors().first().ok_or_else(|| {
            candle_core::Error::Msg("safetensors file is empty".to_string())
        })?.1.dtype();
        let dtype = match first_st_dtype {
            StDtype::F32 => DType::F32,
            StDtype::BF16 => DType::BF16,
            StDtype::F16 => DType::F16,
            dt => return Err(candle_core::Error::Msg(format!("unsupported dtype in safetensors: {:?}", dt))),
        };

        for (name, view) in st.tensors() {
            let shape = view.shape().to_vec();
            let flat = st_view_to_f32(&view)?;
            let tensor = Tensor::from_vec(flat, shape.as_slice(), device)?.to_dtype(dtype)?;
            insert_var(&varmap, &name, tensor);
        }
        let causal_mask = build_causal_mask(config.block_size, device)?.to_dtype(dtype)?;
        let weights = resolve_weights(&varmap, &config);

        Ok(TensorGpt {
            varmap,
            weights,
            vocab_size,
            config,
            device: device.clone(),
            dtype,
            causal_mask,
        })
    }
}

/// Convert a safetensors TensorView to Vec<f32>, handling f32, bf16, and f16 input.
pub fn st_view_to_f32(view: &TensorView) -> Result<Vec<f32>> {
    match view.dtype() {
        StDtype::F32 => Ok(view
            .data()
            .chunks_exact(4)
            .map(|c| f32::from_le_bytes(c.try_into().unwrap()))
            .collect()),
        StDtype::BF16 => Ok(view
            .data()
            .chunks_exact(2)
            .map(|c| {
                let bits = u16::from_le_bytes(c.try_into().unwrap());
                half::bf16::from_bits(bits).to_f32()
            })
            .collect()),
        StDtype::F16 => Ok(view
            .data()
            .chunks_exact(2)
            .map(|c| {
                let bits = u16::from_le_bytes(c.try_into().unwrap());
                half::f16::from_bits(bits).to_f32()
            })
            .collect()),
        other => Err(candle_core::Error::Msg(format!("unsupported safetensors dtype: {other:?}"))),
    }
}

/// Serialize tensors from a HashMap<String, Vec<Vec<f64>>> (InferenceGpt state_dict)
/// to safetensors bytes in the given dtype (F32 or F16).
pub fn serialize_state_dict_st(
    state_dict: &HashMap<String, Vec<Vec<f64>>>,
    dtype: StDtype,
) -> Result<Vec<u8>> {
    let mut buffers: Vec<(String, Vec<usize>, Vec<u8>)> = Vec::new();
    for (name, mat) in state_dict {
        let rows = mat.len();
        let cols = if rows > 0 { mat[0].len() } else { 0 };
        let bytes: Vec<u8> = match dtype {
            StDtype::F32 => mat
                .iter()
                .flatten()
                .flat_map(|&v| (v as f32).to_le_bytes())
                .collect(),
            StDtype::F16 => mat
                .iter()
                .flatten()
                .flat_map(|&v| half::f16::from_f32(v as f32).to_le_bytes())
                .collect(),
            other => return Err(candle_core::Error::Msg(format!("unsupported export dtype: {other:?}"))),
        };
        buffers.push((name.clone(), vec![rows, cols], bytes));
    }
    let tensors: Vec<(String, TensorView)> = buffers
        .iter()
        .map(|(name, shape, bytes)| {
            TensorView::new(dtype, shape.clone(), bytes)
                .map(|tv| (name.clone(), tv))
                .map_err(|e| candle_core::Error::Msg(format!("TensorView: {e}")))
        })
        .collect::<Result<Vec<_>>>()?;
    safetensors::tensor::serialize(tensors, None)
        .map_err(|e| candle_core::Error::Msg(format!("serialize: {e}")))
}

/// Convert a candle DType to the corresponding safetensors Dtype.
fn candle_dtype_to_st(dtype: DType) -> Result<StDtype> {
    match dtype {
        DType::F32 => Ok(StDtype::F32),
        DType::BF16 => Ok(StDtype::BF16),
        DType::F16 => Ok(StDtype::F16),
        other => Err(candle_core::Error::Msg(format!("unsupported dtype for serialization: {other:?}"))),
    }
}

/// Serialize a tensor to raw bytes in the given dtype.
fn tensor_to_bytes(t: &Tensor, dtype: DType) -> Result<Vec<u8>> {
    let t = t.flatten_all()?.to_dtype(dtype)?;
    match dtype {
        DType::F32 => {
            let flat: Vec<f32> = t.to_vec1::<f32>()?;
            Ok(flat.iter().flat_map(|v| v.to_le_bytes()).collect())
        }
        DType::BF16 => {
            let flat: Vec<half::bf16> = t.to_vec1::<half::bf16>()?;
            Ok(flat.iter().flat_map(|v| v.to_le_bytes()).collect())
        }
        DType::F16 => {
            let flat: Vec<half::f16> = t.to_vec1::<half::f16>()?;
            Ok(flat.iter().flat_map(|v| v.to_le_bytes()).collect())
        }
        other => Err(candle_core::Error::Msg(format!("unsupported dtype for serialization: {other:?}"))),
    }
}

/// RMS normalization along the last dimension.
/// Upcast to F32 for numerical stability when the input is BF16/F16.
fn rmsnorm(x: &Tensor) -> Result<Tensor> {
    let dtype = x.dtype();
    let x_f32 = x.to_dtype(DType::F32)?;
    let x_sq = x_f32.sqr()?;
    let mean = x_sq.mean_keepdim(D::Minus1)?;
    let scale = (mean + 1e-5)?.powf(-0.5)?;
    x_f32.broadcast_mul(&scale)?.to_dtype(dtype)
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
