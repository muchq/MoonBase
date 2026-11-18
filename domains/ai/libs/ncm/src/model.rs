//! Compact NCM variant: bidirectional transformers, residual VQ, level-wise teacher forcing.
use candle_core::{D, DType, Device, Result, Tensor};
use candle_nn::{Module, VarBuilder, VarMap};
use serde::{Deserialize, Serialize};
use std::path::Path;

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Config {
    pub vocab: usize,
    pub length: usize,
    pub prompt_length: usize,
    pub width: usize,
    pub heads: usize,
    pub layers: usize,
    pub codes: usize,
    pub scales: Vec<usize>,
}
impl Default for Config {
    fn default() -> Self {
        Self {
            vocab: 512,
            length: 128,
            prompt_length: 128,
            width: 64,
            heads: 4,
            layers: 2,
            codes: 64,
            scales: vec![1, 2, 4, 8, 16, 32, 64, 128],
        }
    }
}
impl Config {
    pub fn validate(&self) -> Result<()> {
        if self.vocab < 4
            || self.length == 0
            || self.prompt_length == 0
            || self.width == 0
            || self.heads == 0
            || self.width % self.heads != 0
            || self.layers == 0
            || self.codes < 2
        {
            candle_core::bail!("invalid dimensions");
        }
        if self.scales.first() != Some(&1)
            || self.scales.last() != Some(&self.length)
            || self.scales.windows(2).any(|s| s[0] >= s[1])
            || self.scales.iter().any(|&s| s == 0 || self.length % s != 0)
        {
            candle_core::bail!("scales must increase from 1 to length and divide length");
        }
        Ok(())
    }
}
struct Block {
    q: candle_nn::Linear,
    k: candle_nn::Linear,
    v: candle_nn::Linear,
    o: candle_nn::Linear,
    up: candle_nn::Linear,
    down: candle_nn::Linear,
    norm1: candle_nn::LayerNorm,
    norm2: candle_nn::LayerNorm,
    heads: usize,
}
impl Block {
    fn new(c: &Config, vb: VarBuilder<'_>) -> Result<Self> {
        let w = c.width;
        Ok(Self {
            q: candle_nn::linear_no_bias(w, w, vb.pp("q"))?,
            k: candle_nn::linear_no_bias(w, w, vb.pp("k"))?,
            v: candle_nn::linear_no_bias(w, w, vb.pp("v"))?,
            o: candle_nn::linear_no_bias(w, w, vb.pp("o"))?,
            up: candle_nn::linear(w, 4 * w, vb.pp("up"))?,
            down: candle_nn::linear(4 * w, w, vb.pp("down"))?,
            norm1: candle_nn::layer_norm(w, 1e-5, vb.pp("n1"))?,
            norm2: candle_nn::layer_norm(w, 1e-5, vb.pp("n2"))?,
            heads: c.heads,
        })
    }
    fn forward(&self, x: &Tensor) -> Result<Tensor> {
        let (s, w) = x.dims2()?;
        let z = self.norm1.forward(x)?;
        let split = |t: Tensor| {
            t.reshape((s, self.heads, w / self.heads))?
                .transpose(0, 1)?
                .contiguous()
        };
        let q = split(self.q.forward(&z)?)?;
        let k = split(self.k.forward(&z)?)?;
        let v = split(self.v.forward(&z)?)?;
        let scores = (q.matmul(&k.t()?)? / ((w / self.heads) as f64).sqrt())?;
        let a = candle_nn::ops::softmax_last_dim(&scores)?
            .matmul(&v)?
            .transpose(0, 1)?
            .contiguous()?
            .reshape((s, w))?;
        let x = (x + self.o.forward(&a)?)?;
        &x + self
            .down
            .forward(&self.up.forward(&self.norm2.forward(&x)?)?.gelu_erf()?)?
    }
}
struct Stack {
    blocks: Vec<Block>,
}
impl Stack {
    fn new(c: &Config, vb: VarBuilder<'_>) -> Result<Self> {
        Ok(Self {
            blocks: (0..c.layers)
                .map(|i| Block::new(c, vb.pp(i)))
                .collect::<Result<_>>()?,
        })
    }
    fn forward(&self, x: &Tensor) -> Result<Tensor> {
        self.blocks.iter().try_fold(x.clone(), |x, b| b.forward(&x))
    }
}

pub struct Model {
    pub config: Config,
    pub tokenizer_vars: VarMap,
    pub generator_vars: VarMap,
    device: Device,
    text: candle_nn::Embedding,
    position: Tensor,
    codebook: Tensor,
    encoder: Stack,
    decoder: Stack,
    text_head: candle_nn::Linear,
    prompt: candle_nn::Embedding,
    prompt_position: Tensor,
    level: Tensor,
    generator: Stack,
    code_head: candle_nn::Linear,
}
impl Model {
    pub fn new(c: Config, device: &Device, seed: u64) -> Result<Self> {
        c.validate()?;

        let tokenizer_vars = VarMap::new();
        let generator_vars = VarMap::new();
        let t = VarBuilder::from_varmap(&tokenizer_vars, DType::F32, device);
        let g = VarBuilder::from_varmap(&generator_vars, DType::F32, device);
        let init = candle_nn::Init::Randn {
            mean: 0.,
            stdev: 0.02,
        };
        let model = Self {
            text: candle_nn::embedding(c.vocab, c.width, t.pp("text"))?,
            position: t.get_with_hints((c.length, c.width), "position", init)?,
            codebook: t.get_with_hints((c.codes, c.width), "codebook", init)?,
            encoder: Stack::new(&c, t.pp("encoder"))?,
            decoder: Stack::new(&c, t.pp("decoder"))?,
            text_head: candle_nn::linear(c.width, c.vocab, t.pp("head"))?,
            prompt: candle_nn::embedding(c.vocab, c.width, g.pp("prompt"))?,
            prompt_position: g.get_with_hints(
                (c.prompt_length, c.width),
                "prompt_position",
                init,
            )?,
            level: g.get_with_hints((c.scales.len(), c.width), "level", init)?,
            generator: Stack::new(&c, g.pp("generator"))?,
            code_head: candle_nn::linear(c.width, c.codes, g.pp("head"))?,
            config: c,
            tokenizer_vars,
            generator_vars,
            device: device.clone(),
        };
        initialize(&model.tokenizer_vars, seed, device)?;
        initialize(&model.generator_vars, seed.wrapping_add(1), device)?;
        Ok(model)
    }
    fn ids(&self, ids: &[u32]) -> Result<Tensor> {
        Tensor::new(ids, &self.device)
    }
    fn check_target(&self, ids: &[u32]) -> Result<()> {
        if ids.len() != self.config.length || ids.iter().any(|&i| i as usize >= self.config.vocab) {
            candle_core::bail!("target must have length valid vocabulary IDs");
        }
        Ok(())
    }
    fn pool(&self, x: &Tensor, n: usize) -> Result<Tensor> {
        x.reshape((n, self.config.length / n, self.config.width))?
            .mean(1)
    }
    fn expand(&self, x: &Tensor, n: usize) -> Result<Tensor> {
        let indices: Vec<u32> = (0..self.config.length)
            .map(|i| (i * n / self.config.length) as u32)
            .collect();
        x.index_select(&self.ids(&indices)?, 0)
    }
    fn nearest(&self, x: &Tensor) -> Result<Tensor> {
        // Squared Euclidean distance; discrete assignment is deliberately detached.
        let e = self.codebook.detach();
        let x = x.detach();
        let d =
            (x.matmul(&e.t()?)? * -2.)?.broadcast_add(&e.sqr()?.sum(D::Minus1)?.unsqueeze(0)?)?;
        d.argmin(D::Minus1)
    }
    fn quantize(&self, ids: &[u32]) -> Result<(Vec<Vec<u32>>, Tensor, Tensor)> {
        self.check_target(ids)?;
        let z = self
            .encoder
            .forward(&self.text.forward(&self.ids(ids)?)?.add(&self.position)?)?;
        let mut residual = z.clone();
        let mut sum = z.zeros_like()?;
        let mut loss = Tensor::new(0f32, &self.device)?;
        let mut codes = Vec::new();
        for &n in &self.config.scales {
            let r = self.pool(&residual, n)?;
            let index = self.nearest(&r)?;
            let q = self.codebook.index_select(&index, 0)?;
            loss = (loss
                + (&q - r.detach())?.sqr()?.mean_all()?
                + ((&r - q.detach())?.sqr()?.mean_all()? * 0.25)?)?;
            let full = self.expand(&q, n)?;
            sum = (sum + &full)?;
            residual = (residual - full.detach())?;
            codes.push(index.to_vec1::<u32>()?);
        }
        // Straight-through estimator: reconstruction trains the encoder, VQ loss the codebook.
        let straight_through = (&z + (sum - z.detach())?.detach())?;
        Ok((
            codes,
            straight_through,
            (loss / self.config.scales.len() as f64)?,
        ))
    }
    pub fn encode(&self, ids: &[u32]) -> Result<Vec<Vec<u32>>> {
        Ok(self.quantize(ids)?.0)
    }
    fn decode(&self, latent: &Tensor) -> Result<Tensor> {
        self.text_head
            .forward(&self.decoder.forward(&latent.add(&self.position)?)?)
    }
    pub fn reconstruct(&self, ids: &[u32]) -> Result<Vec<u32>> {
        self.decode(&self.quantize(ids)?.1)?
            .argmax(D::Minus1)?
            .to_vec1()
    }
    pub fn reconstruction_loss(&self, ids: &[u32]) -> Result<Tensor> {
        let (_, latent, vq) = self.quantize(ids)?;
        let valid = ids
            .iter()
            .position(|&id| id == crate::data::PAD)
            .unwrap_or(ids.len());
        if valid == 0 {
            candle_core::bail!("target contains only padding");
        }
        candle_nn::loss::cross_entropy(
            &self.decode(&latent)?.narrow(0, 0, valid)?,
            &self.ids(&ids[..valid])?,
        )? + vq
    }
    fn level_logits(&self, prompt: &[u32], accumulated: &Tensor, level: usize) -> Result<Tensor> {
        if prompt.is_empty()
            || prompt.len() > self.config.prompt_length
            || prompt.iter().any(|&i| i as usize >= self.config.vocab)
        {
            candle_core::bail!("prompt empty, too long, or invalid token ID");
        }
        let n = self.config.scales[level];
        let p = self
            .prompt
            .forward(&self.ids(prompt)?)?
            .add(&self.prompt_position.narrow(0, 0, prompt.len())?)?;
        let x = self
            .pool(accumulated, n)?
            .add(&self.pool(&self.position.detach(), n)?)?
            .broadcast_add(&self.level.narrow(0, level, 1)?)?;
        // No target codes enter this pass. All positions at this level can attend each other.
        let joined = Tensor::cat(&[&p, &x], 0)?;
        self.code_head.forward(
            &self
                .generator
                .forward(&joined)?
                .narrow(0, prompt.len(), n)?,
        )
    }
    pub fn generator_loss(&self, prompt: &[u32], target: &[u32]) -> Result<Tensor> {
        let targets = self.encode(target)?;
        let mut accumulated = Tensor::zeros(
            (self.config.length, self.config.width),
            DType::F32,
            &self.device,
        )?;
        let mut loss = Tensor::new(0f32, &self.device)?;
        let mut count = 0;
        for (i, ids) in targets.iter().enumerate() {
            let n = ids.len();
            loss = (loss
                + (candle_nn::loss::cross_entropy(
                    &self.level_logits(prompt, &accumulated, i)?,
                    &self.ids(ids)?,
                )? * n as f64)?)?;
            count += n;
            let q = self.codebook.detach().index_select(&self.ids(ids)?, 0)?;
            accumulated = (accumulated + self.expand(&q, n)?)?.detach();
        }
        loss / count as f64
    }
    pub fn generate(&self, prompt: &[u32]) -> Result<Vec<u32>> {
        let mut accumulated = Tensor::zeros(
            (self.config.length, self.config.width),
            DType::F32,
            &self.device,
        )?;
        for (i, &n) in self.config.scales.iter().enumerate() {
            let ids = self
                .level_logits(prompt, &accumulated, i)?
                .argmax(D::Minus1)?;
            accumulated =
                (accumulated + self.expand(&self.codebook.detach().index_select(&ids, 0)?, n)?)?;
        }
        self.decode(&accumulated)?.argmax(D::Minus1)?.to_vec1()
    }
    pub fn save(&self, dir: &Path) -> crate::Result<()> {
        std::fs::write(
            dir.join("config.json"),
            serde_json::to_vec_pretty(&self.config)?,
        )?;
        self.tokenizer_vars.save(dir.join("vq.safetensors"))?;
        self.generator_vars
            .save(dir.join("generator.safetensors"))?;
        Ok(())
    }
    pub fn load(dir: &Path, device: &Device) -> crate::Result<Self> {
        let c = serde_json::from_slice(&std::fs::read(dir.join("config.json"))?)?;
        let mut model = Self::new(c, device, 42)?;
        model.tokenizer_vars.load(dir.join("vq.safetensors"))?;
        model
            .generator_vars
            .load(dir.join("generator.safetensors"))?;
        Ok(model)
    }
}

// Candle CPU RNG cannot be seeded. Initialize named tensors in sorted order on
// the host for reproducible CPU/accelerator starts without global RNG state.
fn initialize(vars: &VarMap, seed: u64, device: &Device) -> Result<()> {
    let values = vars.data().lock().unwrap();
    let mut names: Vec<_> = values.keys().collect();
    names.sort();
    let mut state = seed.max(1);
    for name in names {
        let var = &values[name];
        let scale = if var.rank() == 2 {
            (3.0 / var.dim(1)? as f32).sqrt()
        } else {
            0.02
        };
        let data: Vec<f32> = (0..var.elem_count())
            .map(|_| {
                if name.ends_with("bias") {
                    return 0.;
                }
                if var.rank() == 1 && name.ends_with("weight") {
                    return 1.;
                }
                state ^= state << 13;
                state ^= state >> 7;
                state ^= state << 17;
                ((state as f64 / u64::MAX as f64) as f32 * 2. - 1.) * scale
            })
            .collect();
        var.set(&Tensor::from_vec(data, var.shape(), device)?)?;
    }
    Ok(())
}
