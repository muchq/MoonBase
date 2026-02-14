use crate::model::{Gpt, KvCache, BLOCK_SIZE};
use crate::value::{self, Value};

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

/// Adam optimizer state.
pub struct Adam {
    pub m: Vec<f64>,
    pub v: Vec<f64>,
}

impl Adam {
    pub fn new(num_params: usize) -> Self {
        Adam {
            m: vec![0.0; num_params],
            v: vec![0.0; num_params],
        }
    }
}

/// Run a single training step on one document. Returns the average loss.
pub fn train_step(
    model: &Gpt,
    tokens: &[usize],
    params: &[Value],
    adam: &mut Adam,
    config: &TrainConfig,
    step: usize,
) -> f64 {
    let n = BLOCK_SIZE.min(tokens.len() - 1);
    let mut kv = KvCache::new();
    let mut losses = Vec::with_capacity(n);

    for pos_id in 0..n {
        let token_id = tokens[pos_id];
        let target_id = tokens[pos_id + 1];
        let logits = model.forward(token_id, pos_id, &mut kv);
        let probs = value::softmax(&logits);
        let loss_t = probs[target_id].log().neg();
        losses.push(loss_t);
    }

    let loss = value::sum_values(&losses).mul_scalar(1.0 / n as f64);
    let loss_val = loss.data();
    loss.backward();

    let lr_t = config.learning_rate * (1.0 - step as f64 / config.num_steps as f64);

    for (i, p) in params.iter().enumerate() {
        let g = p.grad();
        adam.m[i] = config.beta1 * adam.m[i] + (1.0 - config.beta1) * g;
        adam.v[i] = config.beta2 * adam.v[i] + (1.0 - config.beta2) * g * g;
        let m_hat = adam.m[i] / (1.0 - config.beta1.powi((step + 1) as i32));
        let v_hat = adam.v[i] / (1.0 - config.beta2.powi((step + 1) as i32));
        p.set_data(p.data() - lr_t * m_hat / (v_hat.sqrt() + config.eps));
        p.set_grad(0.0);
    }

    loss_val
}

/// Generate a single sample from the model using temperature-controlled sampling.
pub fn generate(
    model: &Gpt,
    bos: usize,
    temperature: f64,
    rng_seed: u64,
    decode: impl Fn(usize) -> Option<char>,
) -> String {
    let mut kv = KvCache::new();
    let mut token_id = bos;
    let mut sample = String::new();
    let mut rng_state = if rng_seed == 0 { 1u64 } else { rng_seed };

    for pos_id in 0..BLOCK_SIZE {
        let logits = model.forward(token_id, pos_id, &mut kv);
        let scaled: Vec<Value> = logits
            .iter()
            .map(|l| l.mul_scalar(1.0 / temperature))
            .collect();
        let probs = value::softmax(&scaled);
        let weights: Vec<f64> = probs.iter().map(|p| p.data()).collect();

        token_id = weighted_sample(&weights, &mut rng_state);

        if token_id == bos {
            break;
        }
        if let Some(ch) = decode(token_id) {
            sample.push(ch);
        }
    }

    sample
}

/// Weighted random sampling (equivalent to Python's random.choices).
fn weighted_sample(weights: &[f64], rng_state: &mut u64) -> usize {
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
