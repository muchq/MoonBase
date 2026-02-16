use std::collections::HashMap;

use serde::{Deserialize, Serialize};

use crate::value::{self, Value};

// Default constants — used when no ModelConfig is provided.
pub const N_EMBD: usize = 16;
pub const N_HEAD: usize = 4;
pub const N_LAYER: usize = 1;
pub const BLOCK_SIZE: usize = 16;
pub const HEAD_DIM: usize = N_EMBD / N_HEAD;

/// Runtime-configurable model hyperparameters.
///
/// Allows different checkpoints to use different sizes instead of being
/// locked to the compile-time constants above.
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
            n_embd: N_EMBD,
            n_head: N_HEAD,
            n_layer: N_LAYER,
            block_size: BLOCK_SIZE,
        }
    }
}

pub type Matrix = Vec<Vec<Value>>;

/// Key-value cache for autoregressive generation (one list per layer).
pub struct KvCache {
    pub keys: Vec<Vec<Vec<Value>>>,
    pub values: Vec<Vec<Vec<Value>>>,
}

impl KvCache {
    pub fn new(config: &ModelConfig) -> Self {
        KvCache {
            keys: (0..config.n_layer).map(|_| Vec::new()).collect(),
            values: (0..config.n_layer).map(|_| Vec::new()).collect(),
        }
    }
}

/// A minimal GPT model with token/positional embeddings, multi-head
/// self-attention, feed-forward MLP, and RMS normalization.
pub struct Gpt {
    pub state_dict: HashMap<String, Matrix>,
    pub vocab_size: usize,
    pub config: ModelConfig,
}

/// Simple xorshift64 RNG for reproducible weight initialization.
pub struct Rng {
    state: u64,
}

impl Rng {
    pub fn new(seed: u64) -> Self {
        Rng {
            state: if seed == 0 { 1 } else { seed },
        }
    }

    pub fn next_u64(&mut self) -> u64 {
        let mut x = self.state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.state = x;
        x
    }

    /// Approximate Gaussian via Box-Muller transform.
    pub fn gauss(&mut self, mean: f64, std: f64) -> f64 {
        let u1 = (self.next_u64() as f64) / (u64::MAX as f64);
        let u2 = (self.next_u64() as f64) / (u64::MAX as f64);
        let u1 = u1.max(1e-15); // avoid log(0)
        let z = (-2.0 * u1.ln()).sqrt() * (2.0 * std::f64::consts::PI * u2).cos();
        mean + std * z
    }
}

fn init_matrix(rng: &mut Rng, rows: usize, cols: usize, std: f64) -> Matrix {
    (0..rows)
        .map(|_| (0..cols).map(|_| Value::new(rng.gauss(0.0, std))).collect())
        .collect()
}

impl Gpt {
    pub fn new(vocab_size: usize, seed: u64) -> Self {
        Self::with_config(vocab_size, seed, ModelConfig::default())
    }

    pub fn with_config(vocab_size: usize, seed: u64, config: ModelConfig) -> Self {
        let mut rng = Rng::new(seed);
        let std = 0.08;
        let n_embd = config.n_embd;
        let mut sd = HashMap::new();

        sd.insert("wte".into(), init_matrix(&mut rng, vocab_size, n_embd, std));
        sd.insert(
            "wpe".into(),
            init_matrix(&mut rng, config.block_size, n_embd, std),
        );
        sd.insert(
            "lm_head".into(),
            init_matrix(&mut rng, vocab_size, n_embd, std),
        );

        for i in 0..config.n_layer {
            sd.insert(
                format!("layer{i}.attn_wq"),
                init_matrix(&mut rng, n_embd, n_embd, std),
            );
            sd.insert(
                format!("layer{i}.attn_wk"),
                init_matrix(&mut rng, n_embd, n_embd, std),
            );
            sd.insert(
                format!("layer{i}.attn_wv"),
                init_matrix(&mut rng, n_embd, n_embd, std),
            );
            sd.insert(
                format!("layer{i}.attn_wo"),
                init_matrix(&mut rng, n_embd, n_embd, std),
            );
            sd.insert(
                format!("layer{i}.mlp_fc1"),
                init_matrix(&mut rng, 4 * n_embd, n_embd, std),
            );
            sd.insert(
                format!("layer{i}.mlp_fc2"),
                init_matrix(&mut rng, n_embd, 4 * n_embd, std),
            );
        }

        Gpt {
            state_dict: sd,
            vocab_size,
            config,
        }
    }

    /// Collect all trainable parameters into a flat vector.
    pub fn params(&self) -> Vec<Value> {
        let key_order = {
            let mut keys: Vec<&String> = self.state_dict.keys().collect();
            keys.sort();
            keys
        };
        key_order
            .iter()
            .flat_map(|k| self.state_dict[*k].iter().flat_map(|row| row.iter().cloned()))
            .collect()
    }

    /// Run one step of the GPT forward pass (single token, autoregressive).
    pub fn forward(&self, token_id: usize, pos_id: usize, kv: &mut KvCache) -> Vec<Value> {
        let n_embd = self.config.n_embd;
        let n_head = self.config.n_head;
        let n_layer = self.config.n_layer;
        let head_dim = self.config.head_dim();

        let tok_emb = &self.state_dict["wte"][token_id];
        let pos_emb = &self.state_dict["wpe"][pos_id];
        let mut x: Vec<Value> = tok_emb
            .iter()
            .zip(pos_emb.iter())
            .map(|(t, p)| t.add(p))
            .collect();
        x = value::rmsnorm(&x);

        for li in 0..n_layer {
            let x_residual = x.clone();
            x = value::rmsnorm(&x);

            let q = value::linear(&x, &self.state_dict[&format!("layer{li}.attn_wq")]);
            let k = value::linear(&x, &self.state_dict[&format!("layer{li}.attn_wk")]);
            let v = value::linear(&x, &self.state_dict[&format!("layer{li}.attn_wv")]);

            kv.keys[li].push(k);
            kv.values[li].push(v);

            let mut x_attn = Vec::with_capacity(n_embd);

            for h in 0..n_head {
                let hs = h * head_dim;
                let q_h = &q[hs..hs + head_dim];
                let k_h: Vec<&[Value]> = kv.keys[li]
                    .iter()
                    .map(|ki| &ki[hs..hs + head_dim])
                    .collect();
                let v_h: Vec<&[Value]> = kv.values[li]
                    .iter()
                    .map(|vi| &vi[hs..hs + head_dim])
                    .collect();

                let scale = (head_dim as f64).sqrt();
                let attn_logits: Vec<Value> = k_h
                    .iter()
                    .map(|kt| {
                        value::sum_values(
                            &q_h.iter()
                                .zip(kt.iter())
                                .map(|(qi, ki)| qi.mul(ki))
                                .collect::<Vec<_>>(),
                        )
                        .mul_scalar(1.0 / scale)
                    })
                    .collect();

                let attn_weights = value::softmax(&attn_logits);

                for j in 0..head_dim {
                    let weighted: Vec<Value> = attn_weights
                        .iter()
                        .zip(v_h.iter())
                        .map(|(aw, vt)| aw.mul(&vt[j]))
                        .collect();
                    x_attn.push(value::sum_values(&weighted));
                }
            }

            x = value::linear(
                &x_attn,
                &self.state_dict[&format!("layer{li}.attn_wo")],
            );
            x = x
                .iter()
                .zip(x_residual.iter())
                .map(|(a, b)| a.add(b))
                .collect();

            let x_residual = x.clone();
            x = value::rmsnorm(&x);
            x = value::linear(&x, &self.state_dict[&format!("layer{li}.mlp_fc1")]);
            x = x.iter().map(|xi| xi.relu()).collect();
            x = value::linear(&x, &self.state_dict[&format!("layer{li}.mlp_fc2")]);
            x = x
                .iter()
                .zip(x_residual.iter())
                .map(|(a, b)| a.add(b))
                .collect();
        }

        value::linear(&x, &self.state_dict["lm_head"])
    }

    /// Serialize model weights to JSON.
    pub fn save_weights(&self) -> String {
        let snapshot: HashMap<String, Vec<Vec<f64>>> = self
            .state_dict
            .iter()
            .map(|(k, mat)| {
                let data: Vec<Vec<f64>> = mat.iter().map(|row| row.iter().map(|v| v.data()).collect()).collect();
                (k.clone(), data)
            })
            .collect();
        serde_json::to_string(&snapshot).expect("serialization should not fail")
    }

    /// Load model weights from JSON, returning a new Gpt with those weights.
    pub fn load_weights(vocab_size: usize, json: &str) -> Result<Self, String> {
        Self::load_weights_with_config(vocab_size, json, ModelConfig::default())
    }

    /// Load model weights from JSON with a specific config.
    pub fn load_weights_with_config(
        vocab_size: usize,
        json: &str,
        config: ModelConfig,
    ) -> Result<Self, String> {
        let snapshot: HashMap<String, Vec<Vec<f64>>> =
            serde_json::from_str(json).map_err(|e| format!("failed to parse weights: {e}"))?;
        let sd: HashMap<String, Matrix> = snapshot
            .into_iter()
            .map(|(k, mat)| {
                let values: Matrix = mat
                    .into_iter()
                    .map(|row| row.into_iter().map(Value::new).collect())
                    .collect();
                (k, values)
            })
            .collect();
        Ok(Gpt {
            state_dict: sd,
            vocab_size,
            config,
        })
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
    pub chars: Vec<char>,
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
    /// Load from JSON weights file using default config.
    pub fn load_weights(vocab_size: usize, json: &str) -> Result<Self, String> {
        Self::load_weights_with_config(vocab_size, json, ModelConfig::default())
    }

    /// Load from JSON weights file with a specific config.
    pub fn load_weights_with_config(
        vocab_size: usize,
        json: &str,
        config: ModelConfig,
    ) -> Result<Self, String> {
        let state_dict: HashMap<String, FMatrix> =
            serde_json::from_str(json).map_err(|e| format!("failed to parse weights: {e}"))?;
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
    pub fn generate(
        &self,
        bos: usize,
        temperature: f64,
        rng_seed: u64,
        decode: impl Fn(usize) -> Option<char>,
    ) -> String {
        let mut kv = InferenceKvCache::new(&self.config);
        let mut token_id = bos;
        let mut sample = String::new();
        let mut rng_state = if rng_seed == 0 { 1u64 } else { rng_seed };

        for pos_id in 0..self.config.block_size {
            let logits = self.forward(token_id, pos_id, &mut kv);
            let scaled: Vec<f64> = logits.iter().map(|l| l / temperature).collect();
            let probs = f_softmax(&scaled);

            token_id = weighted_sample_f64(&probs, &mut rng_state);

            if token_id == bos {
                break;
            }
            if let Some(ch) = decode(token_id) {
                sample.push(ch);
            }
        }

        sample
    }

    /// Generate from a prompt using temperature-controlled sampling.
    ///
    /// Encodes the prompt into the KV cache ("prefill"), then samples tokens
    /// one at a time until `stop_token` is emitted or the context window is
    /// exhausted. Calls `on_token` for each generated token (for streaming).
    pub fn generate_from_prompt(
        &self,
        prompt_tokens: &[usize],
        stop_token: usize,
        temperature: f64,
        rng_seed: u64,
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

        for pos in (prompt_tokens.len() - 1)..block_size {
            let logits = self.forward(token_id, pos, &mut kv);
            let scaled: Vec<f64> = logits.iter().map(|l| l / temperature).collect();
            let probs = f_softmax(&scaled);
            token_id = weighted_sample_f64(&probs, &mut rng_state);

            if token_id == stop_token {
                break;
            }
            on_token(token_id);
            output.push(token_id);
        }

        output
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

    #[test]
    fn model_config_default_matches_constants() {
        let cfg = ModelConfig::default();
        assert_eq!(cfg.n_embd, N_EMBD);
        assert_eq!(cfg.n_head, N_HEAD);
        assert_eq!(cfg.n_layer, N_LAYER);
        assert_eq!(cfg.block_size, BLOCK_SIZE);
        assert_eq!(cfg.head_dim(), HEAD_DIM);
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

    #[test]
    fn gpt_new_uses_default_config() {
        let model = Gpt::new(10, 42);
        assert_eq!(model.config.n_embd, N_EMBD);
        assert_eq!(model.vocab_size, 10);
    }

    #[test]
    fn gpt_with_config_uses_custom() {
        let cfg = ModelConfig {
            n_embd: 8,
            n_head: 2,
            n_layer: 1,
            block_size: 8,
        };
        let model = Gpt::with_config(5, 42, cfg);
        assert_eq!(model.config.n_embd, 8);
        assert_eq!(model.config.block_size, 8);
        // wte should have 5 rows (vocab_size)
        assert_eq!(model.state_dict["wte"].len(), 5);
        // each row should have 8 elements (n_embd)
        assert_eq!(model.state_dict["wte"][0].len(), 8);
        // wpe should have block_size rows
        assert_eq!(model.state_dict["wpe"].len(), 8);
    }

    #[test]
    fn gpt_forward_produces_vocab_sized_logits() {
        let model = Gpt::new(10, 42);
        let mut kv = KvCache::new(&model.config);
        let logits = model.forward(0, 0, &mut kv);
        assert_eq!(logits.len(), 10);
    }

    #[test]
    fn gpt_save_load_roundtrip() {
        let model = Gpt::new(5, 42);
        let json = model.save_weights();
        let loaded = Gpt::load_weights(5, &json).unwrap();
        // Check that a weight value survived the roundtrip
        let orig = model.state_dict["wte"][0][0].data();
        let restored = loaded.state_dict["wte"][0][0].data();
        assert!((orig - restored).abs() < 1e-10);
    }

    #[test]
    fn inference_gpt_forward_matches_gpt() {
        let model = Gpt::new(5, 42);
        let json = model.save_weights();
        let inf = InferenceGpt::load_weights(5, &json).unwrap();

        let mut kv1 = KvCache::new(&model.config);
        let mut kv2 = InferenceKvCache::new(&inf.config);

        let logits1 = model.forward(0, 0, &mut kv1);
        let logits2 = inf.forward(0, 0, &mut kv2);

        for (a, b) in logits1.iter().zip(logits2.iter()) {
            assert!((a.data() - b).abs() < 1e-10);
        }
    }

    #[test]
    fn generate_from_prompt_respects_stop_token() {
        // With a random model the output is unpredictable, but we can
        // verify that if the stop token is the BOS token (very likely to
        // be sampled eventually), the output is bounded.
        let model = Gpt::new(5, 42);
        let json = model.save_weights();
        let inf = InferenceGpt::load_weights(5, &json).unwrap();

        let stop_token = 4; // BOS for vocab_size=5
        let prompt = vec![stop_token, 0]; // BOS then token 0

        let mut streamed = Vec::new();
        let output = inf.generate_from_prompt(
            &prompt,
            stop_token,
            0.5,
            42,
            |tok| streamed.push(tok),
        );

        // Streamed tokens should match returned tokens.
        assert_eq!(output, streamed);
        // Output should not exceed block_size - prompt_len + 1
        // (the decode loop starts at the last prompt position).
        assert!(output.len() <= BLOCK_SIZE - prompt.len() + 1);
    }

    #[test]
    fn generate_from_prompt_streaming_callback_fires() {
        let model = Gpt::new(5, 99);
        let json = model.save_weights();
        let inf = InferenceGpt::load_weights(5, &json).unwrap();

        let mut count = 0;
        let output = inf.generate_from_prompt(
            &[4, 0, 1], // BOS + two tokens
            4,           // stop at BOS
            0.8,
            123,
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
        let model = Gpt::with_config(5, 42, cfg);
        let json = model.save_weights();
        let inf = InferenceGpt::load_weights_with_config(5, &json, cfg).unwrap();

        let output = inf.generate_from_prompt(&[4, 0], 4, 0.5, 42, |_| {});
        // Should respect block_size=8, so output <= 8 - 2 + 1 = 7 tokens
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
            chars: vec!['a', 'b'],
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
            chars: vec!['a'],
            special_tokens: Some(vec!["user".into(), "assistant".into(), "end_turn".into()]),
        };
        let json = serde_json::to_string(&meta).unwrap();
        let loaded: ModelMeta = serde_json::from_str(&json).unwrap();
        assert_eq!(loaded.special_tokens.as_ref().unwrap().len(), 3);
    }

    #[test]
    fn model_meta_serde_without_special_tokens() {
        // Simulate loading an old meta.json that doesn't have special_tokens.
        let json = r#"{"vocab_size":5,"n_embd":16,"n_head":4,"n_layer":1,"block_size":16,"chars":["a","b","c","d"]}"#;
        let meta: ModelMeta = serde_json::from_str(json).unwrap();
        assert!(meta.special_tokens.is_none());
    }
}
