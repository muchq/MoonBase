use crate::Result;
use serde::{Deserialize, Serialize};
use std::{
    collections::HashSet,
    fs,
    io::{BufRead, BufReader},
    path::Path,
};
use tokenizers::{
    AddedToken, Tokenizer,
    models::bpe::{BPE, BpeTrainer},
    pre_tokenizers::byte_level::ByteLevel,
};

pub const PAD: u32 = 0;
pub const EOS: u32 = 1;
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Record {
    pub language: String,
    pub prompt: String,
    pub completion: String,
}
impl Record {
    pub fn validate(&self) -> Result<()> {
        if self.language.trim().is_empty() || self.completion.trim().is_empty() {
            return Err("language and completion must not be empty".into());
        }
        Ok(())
    }
    pub fn context(&self) -> String {
        format!("language: {}\n{}", self.language, self.prompt)
    }
}
pub fn read_records(path: &Path) -> Result<Vec<Record>> {
    let mut records = Vec::new();
    for (i, line) in BufReader::new(fs::File::open(path)?).lines().enumerate() {
        let line = line?;
        if line.trim().is_empty() {
            continue;
        }
        let r: Record = serde_json::from_str(&line).map_err(|e| format!("line {}: {e}", i + 1))?;
        r.validate().map_err(|e| format!("line {}: {e}", i + 1))?;
        records.push(r);
    }
    if records.is_empty() {
        return Err("empty dataset".into());
    }
    Ok(records)
}
pub fn train_bpe(records: &[Record], vocab: usize) -> Result<Tokenizer> {
    if vocab < 258 {
        return Err("byte BPE requires vocab >= 258".into());
    }
    type Typed = tokenizers::TokenizerImpl<
        BPE,
        tokenizers::NormalizerWrapper,
        tokenizers::PreTokenizerWrapper,
        tokenizers::PostProcessorWrapper,
        tokenizers::DecoderWrapper,
    >;
    let mut t = Typed::new(BPE::default());
    t.with_pre_tokenizer(Some(ByteLevel::default().add_prefix_space(false)));
    t.with_decoder(Some(tokenizers::decoders::byte_level::ByteLevel::default()));
    let mut trainer = BpeTrainer::builder()
        .vocab_size(vocab)
        .show_progress(false)
        .initial_alphabet(ByteLevel::alphabet().into_iter().collect())
        .special_tokens(vec![
            AddedToken::from("<pad>", true),
            AddedToken::from("<eos>", true),
        ])
        .build();
    let texts: Vec<String> = records
        .iter()
        .flat_map(|r| [r.context(), r.completion.clone()])
        .collect();
    t.train(&mut trainer, texts.into_iter())?;
    Ok(t.into())
}
pub fn encode(t: &Tokenizer, text: &str, limit: usize, pad: bool) -> Result<Vec<u32>> {
    let mut ids = t.encode(text, false)?.get_ids().to_vec();
    ids.push(EOS);
    if ids.len() > limit {
        return Err(format!(
            "{} tokens exceeds limit {limit}; split data or increase context",
            ids.len()
        )
        .into());
    }
    if pad {
        ids.resize(limit, PAD);
    }
    Ok(ids)
}
pub fn decode(t: &Tokenizer, ids: &[u32]) -> Result<String> {
    let end = ids
        .iter()
        .position(|&i| i == EOS || i == PAD)
        .unwrap_or(ids.len());
    t.decode(&ids[..end], true)
}

/// File-level examples; do not silently split classes or remove comments.
/// Symlinks, hidden directories, build outputs and oversized files are skipped.
pub fn collect(
    root: &Path,
    language: &str,
    extensions: &[String],
    max_bytes: u64,
) -> Result<Vec<Record>> {
    fn visit(
        path: &Path,
        files: &mut Vec<std::path::PathBuf>,
        ext: &[String],
        max: u64,
    ) -> Result<()> {
        let meta = fs::symlink_metadata(path)?;
        if meta.file_type().is_symlink() {
            return Ok(());
        }
        if meta.is_dir() {
            for entry in fs::read_dir(path)? {
                let entry = entry?;
                let name = entry.file_name().to_string_lossy().to_string();
                if name.starts_with('.')
                    || ["target", "node_modules", "build", "dist", "vendor"]
                        .contains(&name.as_str())
                {
                    continue;
                }
                visit(&entry.path(), files, ext, max)?;
            }
        } else if meta.is_file()
            && meta.len() <= max
            && path
                .extension()
                .and_then(|s| s.to_str())
                .is_some_and(|s| ext.iter().any(|e| e == s))
        {
            files.push(path.into());
        }
        Ok(())
    }
    if language.trim().is_empty() || extensions.is_empty() {
        return Err("language and extensions required".into());
    }
    let mut files = Vec::new();
    visit(root, &mut files, extensions, max_bytes)?;
    files.sort();
    let mut seen = HashSet::new();
    let mut out = Vec::new();
    for file in files {
        let text = fs::read_to_string(file)?;
        if text.trim().is_empty() || !seen.insert(text.clone()) {
            continue;
        }
        out.push(Record {
            language: language.into(),
            prompt: "Generate a source file.".into(),
            completion: text,
        });
    }
    if out.is_empty() {
        return Err("no matching nonempty source files".into());
    }
    Ok(out)
}
