use crate::{
    Result, data,
    model::{Config, Model},
};
use candle_core::Device;
use candle_nn::{AdamW, Optimizer, ParamsAdamW};
use serde::{Deserialize, Serialize};
use std::{fs, path::Path};

fn optimizer(vars: &candle_nn::VarMap, steps: usize, lr: f64) -> Result<AdamW> {
    if steps == 0 || !lr.is_finite() || lr <= 0. {
        return Err("steps and finite learning rate must be positive".into());
    }
    Ok(AdamW::new(
        vars.all_vars(),
        ParamsAdamW {
            lr,
            weight_decay: 0.01,
            ..Default::default()
        },
    )?)
}
pub fn fit_tokenizer(m: &Model, targets: &[Vec<u32>], steps: usize, lr: f64) -> Result<()> {
    if targets.is_empty() {
        return Err("empty training set".into());
    }
    let mut opt = optimizer(&m.tokenizer_vars, steps, lr)?;
    for i in 0..steps {
        let loss = m.reconstruction_loss(&targets[i % targets.len()])?;
        let value = loss.to_scalar::<f32>()?;
        if !value.is_finite() {
            return Err(format!("nonfinite tokenizer loss at step {i}").into());
        }
        opt.backward_step(&loss)?;
        if i % 50 == 0 || i + 1 == steps {
            eprintln!("tokenizer step={} loss={value:.5}", i + 1);
        }
    }
    Ok(())
}
pub fn fit_generator(
    m: &Model,
    pairs: &[(Vec<u32>, Vec<u32>)],
    steps: usize,
    lr: f64,
) -> Result<()> {
    if pairs.is_empty() {
        return Err("empty training set".into());
    }
    let mut opt = optimizer(&m.generator_vars, steps, lr)?;
    for i in 0..steps {
        let (p, t) = &pairs[i % pairs.len()];
        let loss = m.generator_loss(p, t)?;
        let value = loss.to_scalar::<f32>()?;
        if !value.is_finite() {
            return Err(format!("nonfinite generator loss at step {i}").into());
        }
        opt.backward_step(&loss)?;
        if i % 50 == 0 || i + 1 == steps {
            eprintln!("generator step={} loss={value:.5}", i + 1);
        }
    }
    Ok(())
}
#[derive(Serialize, Deserialize)]
pub struct Manifest {
    pub format_version: u32,
    pub stage: String,
    pub steps: usize,
    pub learning_rate: f64,
    pub seed: u64,
    pub examples: usize,
}
pub fn manifest(dir: &Path) -> Result<Manifest> {
    let m: Manifest = serde_json::from_slice(&fs::read(dir.join("manifest.json"))?)?;
    if m.format_version != 1 || !["tokenizer", "generator"].contains(&m.stage.as_str()) {
        return Err("unsupported checkpoint format/stage".into());
    }
    Ok(m)
}
// Publish only complete bundles. Refuse to overwrite a trained tokenizer or existing model.
fn publish(
    model: &Model,
    tokenizer: &tokenizers::Tokenizer,
    out: &Path,
    manifest: &Manifest,
) -> Result<()> {
    if out.exists() {
        return Err("output already exists; use a new directory".into());
    }
    let parent = out
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    fs::create_dir_all(parent)?;
    let name = out
        .file_name()
        .ok_or("invalid output directory")?
        .to_string_lossy();
    let staging = parent.join(format!(".{name}.partial-{}", std::process::id()));
    fs::create_dir(&staging)?;
    model.save(&staging)?;
    tokenizer.save(staging.join("tokenizer.json"), true)?;
    fs::write(
        staging.join("manifest.json"),
        serde_json::to_vec_pretty(manifest)?,
    )?;
    fs::rename(staging, out)?;
    Ok(())
}
pub fn train_tokenizer(
    input: &Path,
    out: &Path,
    mut c: Config,
    steps: usize,
    lr: f64,
    seed: u64,
    device: &Device,
) -> Result<()> {
    if out.exists() {
        return Err("output already exists".into());
    }
    c.validate()?;
    let records = data::read_records(input)?;
    let tokenizer = data::train_bpe(&records, c.vocab)?;
    c.vocab = tokenizer.get_vocab_size(true);
    let targets = records
        .iter()
        .map(|r| data::encode(&tokenizer, &r.completion, c.length, true))
        .collect::<Result<Vec<_>>>()?;
    let model = Model::new(c, device, seed)?;
    fit_tokenizer(&model, &targets, steps, lr)?;
    publish(
        &model,
        &tokenizer,
        out,
        &Manifest {
            format_version: 1,
            stage: "tokenizer".into(),
            steps,
            learning_rate: lr,
            seed,
            examples: records.len(),
        },
    )
}
pub fn train_generator(
    input: &Path,
    source: &Path,
    out: &Path,
    steps: usize,
    lr: f64,
    device: &Device,
) -> Result<()> {
    if out.exists() {
        return Err("output already exists".into());
    }
    let previous = manifest(source)?;
    let model = Model::load(source, device)?;
    let tokenizer = tokenizers::Tokenizer::from_file(source.join("tokenizer.json"))?;
    let records = data::read_records(input)?;
    let pairs = records
        .iter()
        .map(|r| {
            Ok((
                data::encode(&tokenizer, &r.context(), model.config.prompt_length, false)?,
                data::encode(&tokenizer, &r.completion, model.config.length, true)?,
            ))
        })
        .collect::<Result<Vec<_>>>()?;
    fit_generator(&model, &pairs, steps, lr)?;
    publish(
        &model,
        &tokenizer,
        out,
        &Manifest {
            format_version: 1,
            stage: "generator".into(),
            steps,
            learning_rate: lr,
            seed: previous.seed,
            examples: records.len(),
        },
    )
}
#[derive(Serialize)]
pub struct Metrics {
    pub examples: usize,
    pub reconstruction_loss: f64,
    pub generator_loss: f64,
    pub reconstruction_token_accuracy: f64,
    pub generation_token_accuracy: f64,
    pub generation_exact_match: f64,
    pub used_codes: Vec<usize>,
}
pub fn evaluate(input: &Path, source: &Path, device: &Device) -> Result<Metrics> {
    manifest(source)?;
    let m = Model::load(source, device)?;
    let tokenizer = tokenizers::Tokenizer::from_file(source.join("tokenizer.json"))?;
    let records = data::read_records(input)?;
    let mut result = Metrics {
        examples: records.len(),
        reconstruction_loss: 0.,
        generator_loss: 0.,
        reconstruction_token_accuracy: 0.,
        generation_token_accuracy: 0.,
        generation_exact_match: 0.,
        used_codes: vec![],
    };
    let mut used: Vec<std::collections::HashSet<u32>> =
        m.config.scales.iter().map(|_| Default::default()).collect();
    let mut total = 0;
    for r in &records {
        let t = data::encode(&tokenizer, &r.completion, m.config.length, true)?;
        let p = data::encode(&tokenizer, &r.context(), m.config.prompt_length, false)?;
        result.reconstruction_loss += m.reconstruction_loss(&t)?.to_scalar::<f32>()? as f64;
        result.generator_loss += m.generator_loss(&p, &t)?.to_scalar::<f32>()? as f64;
        let reconstructed = m.reconstruct(&t)?;
        let generated = m.generate(&p)?;
        for (i, &id) in t.iter().enumerate().filter(|(_, id)| **id != data::PAD) {
            total += 1;
            result.reconstruction_token_accuracy += f64::from(reconstructed[i] == id);
            result.generation_token_accuracy += f64::from(generated[i] == id);
        }
        result.generation_exact_match +=
            f64::from(data::decode(&tokenizer, &generated)? == r.completion);
        for (set, codes) in used.iter_mut().zip(m.encode(&t)?) {
            set.extend(codes);
        }
    }
    result.reconstruction_loss /= records.len() as f64;
    result.generator_loss /= records.len() as f64;
    result.reconstruction_token_accuracy /= total as f64;
    result.generation_token_accuracy /= total as f64;
    result.generation_exact_match /= records.len() as f64;
    result.used_codes = used.iter().map(|s| s.len()).collect();
    Ok(result)
}
pub struct LocalBackend {
    model: Model,
    tokenizer: tokenizers::Tokenizer,
    language: String,
}
impl LocalBackend {
    pub fn load(source: &Path, language: String, device: &Device) -> Result<Self> {
        if manifest(source)?.stage != "generator" {
            return Err("train the generator before inference".into());
        }
        Ok(Self {
            model: Model::load(source, device)?,
            tokenizer: tokenizers::Tokenizer::from_file(source.join("tokenizer.json"))?,
            language,
        })
    }
    pub fn generate(&self, prompt: &str) -> Result<String> {
        let context = format!("language: {}\n{prompt}", self.language);
        let ids = data::encode(
            &self.tokenizer,
            &context,
            self.model.config.prompt_length,
            false,
        )?;
        data::decode(&self.tokenizer, &self.model.generate(&ids)?)
    }
}
impl crate::agent::Backend for LocalBackend {
    fn complete(&mut self, prompt: &str) -> Result<String> {
        self.generate(prompt)
    }
}
