use std::collections::HashMap;

use safetensors::SafeTensors;
use serde::{Deserialize, Serialize};

use crate::tensor_model::st_view_to_f32;

/// Runtime-configurable model hyperparameters.
#[derive(Clone, Copy, Debug, Serialize, Deserialize)]
pub struct ModelConfig {
    pub n_embd: usize,
    pub n_head: usize,
    pub n_layer: usize,
    pub block_size: usize,
}

impl ModelConfig {
    pub fn head_dim(&self) -> usize {
        self.n_embd / self.n_head
    }
}

impl Default for ModelConfig {
    fn default() -> Self {
        ModelConfig {
            n_embd: 16,
            n_head: 4,
            n_layer: 1,
            block_size: 16,
        }
    }
}

/// Serializable model metadata (for saving alongside weights).
#[derive(Serialize, Deserialize)]
pub struct ModelMeta {
    pub vocab_size: usize,
    pub n_embd: usize,
    pub n_head: usize,
    pub n_layer: usize,
    pub block_size: usize,
    /// Names of special tokens (e.g. ["user", "assistant", "end_turn"]).
    /// Present only for models trained with chat support.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub special_tokens: Option<Vec<String>>,
}

impl ModelMeta {
    /// Extract the model config from saved metadata.
    pub fn config(&self) -> ModelConfig {
        ModelConfig {
            n_embd: self.n_embd,
            n_head: self.n_head,
            n_layer: self.n_layer,
            block_size: self.block_size,
        }
    }
}

// ---------------------------------------------------------------------------
// Inference-only model (plain f64, Send + Sync, no autograd overhead)
// ---------------------------------------------------------------------------

pub type FMatrix = Vec<Vec<f64>>;

/// KV cache for inference (plain f64).
pub struct InferenceKvCache {
    pub keys: Vec<Vec<Vec<f64>>>,
    pub values: Vec<Vec<Vec<f64>>>,
}

impl InferenceKvCache {
    pub fn new(config: &ModelConfig) -> Self {
        InferenceKvCache {
            keys: (0..config.n_layer).map(|_| Vec::new()).collect(),
            values: (0..config.n_layer).map(|_| Vec::new()).collect(),
        }
    }
}

/// Inference-only GPT that uses plain `f64` arithmetic.
/// This is `Send + Sync` and suitable for use behind `Arc` in async servers.
pub struct InferenceGpt {
    pub state_dict: HashMap<String, FMatrix>,
    pub vocab_size: usize,
    pub config: ModelConfig,
}

fn f_linear(x: &[f64], w: &[Vec<f64>]) -> Vec<f64> {
    w.iter()
        .map(|row| row.iter().zip(x.iter()).map(|(wi, xi)| wi * xi).sum())
        .collect()
}

fn f_softmax(logits: &[f64]) -> Vec<f64> {
    let max_val = logits.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
    let exps: Vec<f64> = logits.iter().map(|v| (v - max_val).exp()).collect();
    let total: f64 = exps.iter().sum();
    exps.iter().map(|e| e / total).collect()
}

fn f_rmsnorm(x: &[f64]) -> Vec<f64> {
    let n = x.len() as f64;
    let ms: f64 = x.iter().map(|xi| xi * xi).sum::<f64>() / n;
    let scale = (ms + 1e-5).powf(-0.5);
    x.iter().map(|xi| xi * scale).collect()
}

impl InferenceGpt {
    /// Load from safetensors bytes. Handles both f32 and f16 input.
    pub fn load_safetensors(
        vocab_size: usize,
        bytes: &[u8],
        config: ModelConfig,
    ) -> Result<Self, String> {
        let st = SafeTensors::deserialize(bytes)
            .map_err(|e| format!("failed to load safetensors: {e}"))?;
        let mut state_dict = HashMap::new();
        for (name, view) in st.tensors() {
            let shape = view.shape();
            let rows = shape[0];
            let cols = shape[1];
            let flat = st_view_to_f32(&view)
                .map_err(|e| format!("failed to decode tensor {name}: {e}"))?;
            let mat: FMatrix = (0..rows)
                .map(|r| {
                    flat[r * cols..(r + 1) * cols]
                        .iter()
                        .map(|&v| v as f64)
                        .collect()
                })
                .collect();
            state_dict.insert(name.to_string(), mat);
        }
        Ok(InferenceGpt {
            state_dict,
            vocab_size,
            config,
        })
    }

    /// Count parameters.
    pub fn num_params(&self) -> usize {
        self.state_dict
            .values()
            .map(|m| m.iter().map(|r| r.len()).sum::<usize>())
            .sum()
    }

    /// Forward pass for a single token (autoregressive, no autograd).
    pub fn forward(&self, token_id: usize, pos_id: usize, kv: &mut InferenceKvCache) -> Vec<f64> {
        let n_embd = self.config.n_embd;
        let n_head = self.config.n_head;
        let n_layer = self.config.n_layer;
        let head_dim = self.config.head_dim();

        let tok_emb = &self.state_dict["wte"][token_id];
        let pos_emb = &self.state_dict["wpe"][pos_id];
        let mut x: Vec<f64> =
            tok_emb.iter().zip(pos_emb.iter()).map(|(t, p)| t + p).collect();
        x = f_rmsnorm(&x);

        for li in 0..n_layer {
            let x_residual = x.clone();
            x = f_rmsnorm(&x);

            let q = f_linear(&x, &self.state_dict[&format!("layer{li}.attn_wq")]);
            let k = f_linear(&x, &self.state_dict[&format!("layer{li}.attn_wk")]);
            let v = f_linear(&x, &self.state_dict[&format!("layer{li}.attn_wv")]);

            kv.keys[li].push(k);
            kv.values[li].push(v);

            let mut x_attn = Vec::with_capacity(n_embd);

            for h in 0..n_head {
                let hs = h * head_dim;
                let q_h = &q[hs..hs + head_dim];
                let k_h: Vec<&[f64]> =
                    kv.keys[li].iter().map(|ki| &ki[hs..hs + head_dim]).collect();
                let v_h: Vec<&[f64]> = kv.values[li]
                    .iter()
                    .map(|vi| &vi[hs..hs + head_dim])
                    .collect();

                let scale = (head_dim as f64).sqrt();
                let attn_logits: Vec<f64> = k_h
                    .iter()
                    .map(|kt| {
                        q_h.iter().zip(kt.iter()).map(|(qi, ki)| qi * ki).sum::<f64>() / scale
                    })
                    .collect();

                let attn_weights = f_softmax(&attn_logits);

                for j in 0..head_dim {
                    let val: f64 = attn_weights
                        .iter()
                        .zip(v_h.iter())
                        .map(|(aw, vt)| aw * vt[j])
                        .sum();
                    x_attn.push(val);
                }
            }

            x = f_linear(&x_attn, &self.state_dict[&format!("layer{li}.attn_wo")]);
            x = x.iter().zip(x_residual.iter()).map(|(a, b)| a + b).collect();

            let x_residual = x.clone();
            x = f_rmsnorm(&x);
            x = f_linear(&x, &self.state_dict[&format!("layer{li}.mlp_fc1")]);
            x = x.iter().map(|xi| xi.max(0.0)).collect();
            x = f_linear(&x, &self.state_dict[&format!("layer{li}.mlp_fc2")]);
            x = x.iter().zip(x_residual.iter()).map(|(a, b)| a + b).collect();
        }

        f_linear(&x, &self.state_dict["lm_head"])
    }

    /// Generate a sample using temperature-controlled sampling.
    /// If `max_tokens` is Some, generation stops after that many tokens
    /// (capped at block_size).
    pub fn generate(
        &self,
        bos: usize,
        temperature: f64,
        rng_seed: u64,
        max_tokens: Option<usize>,
        decode: impl Fn(usize) -> Option<String>,
    ) -> String {
        let mut kv = InferenceKvCache::new(&self.config);
        let mut token_id = bos;
        let mut sample = String::new();
        let mut rng_state = if rng_seed == 0 { 1u64 } else { rng_seed };
        let limit = max_tokens.unwrap_or(self.config.block_size).min(self.config.block_size);

        for pos_id in 0..limit {
            let logits = self.forward(token_id, pos_id, &mut kv);
            token_id = sample_token(&logits, temperature, &mut rng_state);

            if token_id == bos {
                break;
            }
            if let Some(s) = decode(token_id) {
                sample.push_str(&s);
            }
        }

        sample.trim().to_string()
    }

    /// Generate from a prompt using temperature-controlled sampling.
    ///
    /// Encodes the prompt into the KV cache ("prefill"), then samples tokens
    /// one at a time until any token in `stop_tokens` is emitted or the
    /// context window is exhausted. Tokens in `suppress_tokens` are masked
    /// from the sampling distribution (logits set to -inf) so the model
    /// cannot generate them. If `max_tokens` is Some, generation stops
    /// after that many output tokens (capped at block_size - prompt_len).
    /// Calls `on_token` for each generated token (for streaming).
    pub fn generate_from_prompt(
        &self,
        prompt_tokens: &[usize],
        stop_tokens: &[usize],
        suppress_tokens: &[usize],
        temperature: f64,
        rng_seed: u64,
        max_tokens: Option<usize>,
        mut on_token: impl FnMut(usize),
    ) -> Vec<usize> {
        assert!(!prompt_tokens.is_empty(), "prompt must not be empty");
        let block_size = self.config.block_size;
        let mut kv = InferenceKvCache::new(&self.config);
        let mut rng_state = if rng_seed == 0 { 1u64 } else { rng_seed };

        // Prefill: process all prompt tokens except the last (we only need
        // their KV contributions, not their logits).
        for pos in 0..prompt_tokens.len().saturating_sub(1) {
            self.forward(prompt_tokens[pos], pos, &mut kv);
        }

        // Decode: process last prompt token and sample continuation.
        let mut output = Vec::new();
        let mut token_id = *prompt_tokens.last().unwrap();
        let decode_start = prompt_tokens.len() - 1;
        let max_pos = max_tokens
            .map(|n| decode_start + n)
            .unwrap_or(block_size)
            .min(block_size);

        for pos in decode_start..max_pos {
            let mut logits = self.forward(token_id, pos, &mut kv);
            for &id in suppress_tokens {
                if id < logits.len() {
                    logits[id] = f64::NEG_INFINITY;
                }
            }
            // Suppress stop tokens until at least one content token has been
            // generated, preventing undertrained models from immediately
            // emitting end-of-turn on the first position.
            if output.is_empty() {
                for &id in stop_tokens {
                    if id < logits.len() {
                        logits[id] = f64::NEG_INFINITY;
                    }
                }
            }
            token_id = sample_token(&logits, temperature, &mut rng_state);

            if stop_tokens.contains(&token_id) {
                break;
            }
            on_token(token_id);
            output.push(token_id);
        }

        output
    }
}

/// Sample a token from logits. Greedy argmax when temperature <= 0.
fn sample_token(logits: &[f64], temperature: f64, rng_state: &mut u64) -> usize {
    if temperature <= 0.0 {
        logits
            .iter()
            .enumerate()
            .max_by(|(_, a), (_, b)| a.partial_cmp(b).unwrap())
            .map(|(i, _)| i)
            .unwrap_or(0)
    } else {
        let scaled: Vec<f64> = logits.iter().map(|l| l / temperature).collect();
        let probs = f_softmax(&scaled);
        weighted_sample_f64(&probs, rng_state)
    }
}

fn weighted_sample_f64(weights: &[f64], rng_state: &mut u64) -> usize {
    let total: f64 = weights.iter().sum();
    let mut x = *rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *rng_state = x;
    let r = (x as f64 / u64::MAX as f64) * total;

    let mut cumulative = 0.0;
    for (i, &w) in weights.iter().enumerate() {
        cumulative += w;
        if r < cumulative {
            return i;
        }
    }
    weights.len() - 1
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::TensorGpt;

    #[test]
    fn model_config_default_values() {
        let cfg = ModelConfig::default();
        assert_eq!(cfg.n_embd, 16);
        assert_eq!(cfg.n_head, 4);
        assert_eq!(cfg.n_layer, 1);
        assert_eq!(cfg.block_size, 16);
        assert_eq!(cfg.head_dim(), 4);
    }

    #[test]
    fn model_config_custom_head_dim() {
        let cfg = ModelConfig {
            n_embd: 32,
            n_head: 8,
            n_layer: 2,
            block_size: 64,
        };
        assert_eq!(cfg.head_dim(), 4);
    }

    /// Helper: create TensorGpt → save as safetensors → load as InferenceGpt.
    fn tensor_to_inference(vocab_size: usize, seed: u64, config: ModelConfig) -> InferenceGpt {
        let device = candle_core::Device::Cpu;
        let model = TensorGpt::new(vocab_size, seed, config, &device, candle_core::DType::F32);
        let bytes = model.save_weights_st().unwrap();
        InferenceGpt::load_safetensors(vocab_size, &bytes, config).unwrap()
    }

    #[test]
    fn inference_gpt_forward_produces_vocab_sized_logits() {
        let inf = tensor_to_inference(10, 42, ModelConfig::default());
        let mut kv = InferenceKvCache::new(&inf.config);
        let logits = inf.forward(0, 0, &mut kv);
        assert_eq!(logits.len(), 10);
    }

    #[test]
    fn inference_gpt_safetensors_roundtrip() {
        let device = candle_core::Device::Cpu;
        let config = ModelConfig::default();
        let model = TensorGpt::new(5, 42, config, &device, candle_core::DType::F32);
        let bytes = model.save_weights_st().unwrap();
        let inf = InferenceGpt::load_safetensors(5, &bytes, config).unwrap();

        // Re-serialize via serialize_state_dict_st and reload
        let bytes2 = crate::tensor_model::serialize_state_dict_st(
            &inf.state_dict,
            crate::StDtype::F32,
        ).unwrap();
        let inf2 = InferenceGpt::load_safetensors(5, &bytes2, config).unwrap();

        let mut kv1 = InferenceKvCache::new(&inf.config);
        let mut kv2 = InferenceKvCache::new(&inf2.config);
        let logits1 = inf.forward(0, 0, &mut kv1);
        let logits2 = inf2.forward(0, 0, &mut kv2);

        for (a, b) in logits1.iter().zip(logits2.iter()) {
            assert!((a - b).abs() < 1e-10);
        }
    }

    #[test]
    fn inference_gpt_f16_roundtrip() {
        let device = candle_core::Device::Cpu;
        let config = ModelConfig::default();
        let model = TensorGpt::new(5, 42, config, &device, candle_core::DType::F32);
        let bytes_f32 = model.save_weights_st().unwrap();
        let inf_f32 = InferenceGpt::load_safetensors(5, &bytes_f32, config).unwrap();

        // Export as f16 and reload
        let bytes_f16 = crate::tensor_model::serialize_state_dict_st(
            &inf_f32.state_dict,
            crate::StDtype::F16,
        ).unwrap();
        let inf_f16 = InferenceGpt::load_safetensors(5, &bytes_f16, config).unwrap();

        // f16 has lower precision, so use a looser tolerance
        let mut kv1 = InferenceKvCache::new(&inf_f32.config);
        let mut kv2 = InferenceKvCache::new(&inf_f16.config);
        let logits1 = inf_f32.forward(0, 0, &mut kv1);
        let logits2 = inf_f16.forward(0, 0, &mut kv2);

        for (a, b) in logits1.iter().zip(logits2.iter()) {
            assert!(
                (a - b).abs() < 0.1,
                "f16 roundtrip diverged: {a} vs {b}"
            );
        }
    }

    #[test]
    fn generate_from_prompt_respects_stop_token() {
        let inf = tensor_to_inference(5, 42, ModelConfig::default());

        let stop_token = 4; // BOS for vocab_size=5
        let prompt = vec![stop_token, 0];

        let mut streamed = Vec::new();
        let output = inf.generate_from_prompt(
            &prompt,
            &[stop_token],
            &[],
            0.5,
            42,
            None,
            |tok| streamed.push(tok),
        );

        assert_eq!(output, streamed);
        assert!(output.len() <= inf.config.block_size - prompt.len() + 1);
    }

    #[test]
    fn generate_from_prompt_streaming_callback_fires() {
        let inf = tensor_to_inference(5, 99, ModelConfig::default());

        let mut count = 0;
        let output = inf.generate_from_prompt(
            &[4, 0, 1],
            &[4],
            &[],
            0.8,
            123,
            None,
            |_| count += 1,
        );
        assert_eq!(count, output.len());
    }

    #[test]
    fn generate_from_prompt_with_custom_config() {
        let cfg = ModelConfig {
            n_embd: 8,
            n_head: 2,
            n_layer: 1,
            block_size: 8,
        };
        let inf = tensor_to_inference(5, 42, cfg);

        let output = inf.generate_from_prompt(&[4, 0], &[4], &[], 0.5, 42, None, |_| {});
        assert!(output.len() <= 7);
    }

    #[test]
    fn model_meta_config_roundtrip() {
        let meta = ModelMeta {
            vocab_size: 10,
            n_embd: 32,
            n_head: 8,
            n_layer: 2,
            block_size: 64,
            special_tokens: Some(vec![
                "user".to_string(),
                "assistant".to_string(),
                "end_turn".to_string(),
            ]),
        };
        let cfg = meta.config();
        assert_eq!(cfg.n_embd, 32);
        assert_eq!(cfg.n_head, 8);
        assert_eq!(cfg.n_layer, 2);
        assert_eq!(cfg.block_size, 64);
    }

    #[test]
    fn model_meta_serde_with_special_tokens() {
        let meta = ModelMeta {
            vocab_size: 10,
            n_embd: 16,
            n_head: 4,
            n_layer: 1,
            block_size: 16,
            special_tokens: Some(vec!["user".into(), "assistant".into(), "end_turn".into()]),
        };
        let json = serde_json::to_string(&meta).unwrap();
        let loaded: ModelMeta = serde_json::from_str(&json).unwrap();
        assert_eq!(loaded.special_tokens.as_ref().unwrap().len(), 3);
    }

    #[test]
    fn model_meta_serde_without_special_tokens() {
        let json = r#"{"vocab_size":5,"n_embd":16,"n_head":4,"n_layer":1,"block_size":16}"#;
        let meta: ModelMeta = serde_json::from_str(json).unwrap();
        assert!(meta.special_tokens.is_none());
    }
}
