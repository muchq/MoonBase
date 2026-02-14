use std::collections::HashMap;

use serde::{Deserialize, Serialize};

use crate::value::{self, Value};

pub const N_EMBD: usize = 16;
pub const N_HEAD: usize = 4;
pub const N_LAYER: usize = 1;
pub const BLOCK_SIZE: usize = 16;
pub const HEAD_DIM: usize = N_EMBD / N_HEAD;

pub type Matrix = Vec<Vec<Value>>;

/// Key-value cache for autoregressive generation (one list per layer).
pub struct KvCache {
    pub keys: Vec<Vec<Vec<Value>>>,
    pub values: Vec<Vec<Vec<Value>>>,
}

impl KvCache {
    pub fn new() -> Self {
        KvCache {
            keys: (0..N_LAYER).map(|_| Vec::new()).collect(),
            values: (0..N_LAYER).map(|_| Vec::new()).collect(),
        }
    }
}

/// A minimal GPT model with token/positional embeddings, multi-head
/// self-attention, feed-forward MLP, and RMS normalization.
pub struct Gpt {
    pub state_dict: HashMap<String, Matrix>,
    pub vocab_size: usize,
}

/// Simple xorshift64 RNG for reproducible weight initialization.
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

    /// Approximate Gaussian via Box-Muller transform.
    fn gauss(&mut self, mean: f64, std: f64) -> f64 {
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
        let mut rng = Rng::new(seed);
        let std = 0.08;
        let mut sd = HashMap::new();

        sd.insert("wte".into(), init_matrix(&mut rng, vocab_size, N_EMBD, std));
        sd.insert("wpe".into(), init_matrix(&mut rng, BLOCK_SIZE, N_EMBD, std));
        sd.insert(
            "lm_head".into(),
            init_matrix(&mut rng, vocab_size, N_EMBD, std),
        );

        for i in 0..N_LAYER {
            sd.insert(
                format!("layer{i}.attn_wq"),
                init_matrix(&mut rng, N_EMBD, N_EMBD, std),
            );
            sd.insert(
                format!("layer{i}.attn_wk"),
                init_matrix(&mut rng, N_EMBD, N_EMBD, std),
            );
            sd.insert(
                format!("layer{i}.attn_wv"),
                init_matrix(&mut rng, N_EMBD, N_EMBD, std),
            );
            sd.insert(
                format!("layer{i}.attn_wo"),
                init_matrix(&mut rng, N_EMBD, N_EMBD, std),
            );
            sd.insert(
                format!("layer{i}.mlp_fc1"),
                init_matrix(&mut rng, 4 * N_EMBD, N_EMBD, std),
            );
            sd.insert(
                format!("layer{i}.mlp_fc2"),
                init_matrix(&mut rng, N_EMBD, 4 * N_EMBD, std),
            );
        }

        Gpt {
            state_dict: sd,
            vocab_size,
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
        let tok_emb = &self.state_dict["wte"][token_id];
        let pos_emb = &self.state_dict["wpe"][pos_id];
        let mut x: Vec<Value> = tok_emb
            .iter()
            .zip(pos_emb.iter())
            .map(|(t, p)| t.add(p))
            .collect();
        x = value::rmsnorm(&x);

        for li in 0..N_LAYER {
            let x_residual = x.clone();
            x = value::rmsnorm(&x);

            let q = value::linear(&x, &self.state_dict[&format!("layer{li}.attn_wq")]);
            let k = value::linear(&x, &self.state_dict[&format!("layer{li}.attn_wk")]);
            let v = value::linear(&x, &self.state_dict[&format!("layer{li}.attn_wv")]);

            kv.keys[li].push(k);
            kv.values[li].push(v);

            let mut x_attn = Vec::with_capacity(N_EMBD);

            for h in 0..N_HEAD {
                let hs = h * HEAD_DIM;
                let q_h = &q[hs..hs + HEAD_DIM];
                let k_h: Vec<&[Value]> = kv.keys[li]
                    .iter()
                    .map(|ki| &ki[hs..hs + HEAD_DIM])
                    .collect();
                let v_h: Vec<&[Value]> = kv.values[li]
                    .iter()
                    .map(|vi| &vi[hs..hs + HEAD_DIM])
                    .collect();

                let scale = (HEAD_DIM as f64).sqrt();
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

                for j in 0..HEAD_DIM {
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
    pub fn new() -> Self {
        InferenceKvCache {
            keys: (0..N_LAYER).map(|_| Vec::new()).collect(),
            values: (0..N_LAYER).map(|_| Vec::new()).collect(),
        }
    }
}

/// Inference-only GPT that uses plain `f64` arithmetic.
/// This is `Send + Sync` and suitable for use behind `Arc` in async servers.
pub struct InferenceGpt {
    pub state_dict: HashMap<String, FMatrix>,
    pub vocab_size: usize,
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
    /// Load from JSON weights file.
    pub fn load_weights(vocab_size: usize, json: &str) -> Result<Self, String> {
        let state_dict: HashMap<String, FMatrix> =
            serde_json::from_str(json).map_err(|e| format!("failed to parse weights: {e}"))?;
        Ok(InferenceGpt { state_dict, vocab_size })
    }

    /// Count parameters.
    pub fn num_params(&self) -> usize {
        self.state_dict.values().map(|m| m.iter().map(|r| r.len()).sum::<usize>()).sum()
    }

    /// Forward pass for a single token (autoregressive, no autograd).
    pub fn forward(&self, token_id: usize, pos_id: usize, kv: &mut InferenceKvCache) -> Vec<f64> {
        let tok_emb = &self.state_dict["wte"][token_id];
        let pos_emb = &self.state_dict["wpe"][pos_id];
        let mut x: Vec<f64> = tok_emb.iter().zip(pos_emb.iter()).map(|(t, p)| t + p).collect();
        x = f_rmsnorm(&x);

        for li in 0..N_LAYER {
            let x_residual = x.clone();
            x = f_rmsnorm(&x);

            let q = f_linear(&x, &self.state_dict[&format!("layer{li}.attn_wq")]);
            let k = f_linear(&x, &self.state_dict[&format!("layer{li}.attn_wk")]);
            let v = f_linear(&x, &self.state_dict[&format!("layer{li}.attn_wv")]);

            kv.keys[li].push(k);
            kv.values[li].push(v);

            let mut x_attn = Vec::with_capacity(N_EMBD);

            for h in 0..N_HEAD {
                let hs = h * HEAD_DIM;
                let q_h = &q[hs..hs + HEAD_DIM];
                let k_h: Vec<&[f64]> = kv.keys[li].iter().map(|ki| &ki[hs..hs + HEAD_DIM]).collect();
                let v_h: Vec<&[f64]> = kv.values[li].iter().map(|vi| &vi[hs..hs + HEAD_DIM]).collect();

                let scale = (HEAD_DIM as f64).sqrt();
                let attn_logits: Vec<f64> = k_h
                    .iter()
                    .map(|kt| q_h.iter().zip(kt.iter()).map(|(qi, ki)| qi * ki).sum::<f64>() / scale)
                    .collect();

                let attn_weights = f_softmax(&attn_logits);

                for j in 0..HEAD_DIM {
                    let val: f64 = attn_weights.iter().zip(v_h.iter()).map(|(aw, vt)| aw * vt[j]).sum();
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
        let mut kv = InferenceKvCache::new();
        let mut token_id = bos;
        let mut sample = String::new();
        let mut rng_state = if rng_seed == 0 { 1u64 } else { rng_seed };

        for pos_id in 0..BLOCK_SIZE {
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
