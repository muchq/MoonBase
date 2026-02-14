use std::fs;
use std::path::Path;

/// Character-level tokenizer that maps unique characters to integer IDs.
///
/// Mirrors the Python gist's tokenizer: sorted unique characters plus a
/// special Beginning-of-Sequence (BOS) token at `vocab_size - 1`.
pub struct Tokenizer {
    pub chars: Vec<char>,
    pub bos: usize,
    pub vocab_size: usize,
}

impl Tokenizer {
    pub fn from_corpus(text: &str) -> Self {
        let mut chars: Vec<char> = text.chars().collect::<std::collections::BTreeSet<_>>().into_iter().collect();
        chars.sort();
        let bos = chars.len();
        let vocab_size = chars.len() + 1;
        Tokenizer {
            chars,
            bos,
            vocab_size,
        }
    }

    pub fn encode_char(&self, ch: char) -> Option<usize> {
        self.chars.iter().position(|&c| c == ch)
    }

    pub fn decode(&self, id: usize) -> Option<char> {
        if id < self.chars.len() {
            Some(self.chars[id])
        } else {
            None
        }
    }

    /// Encode a document as `[BOS, ...char_ids..., BOS]`.
    pub fn encode_doc(&self, doc: &str) -> Vec<usize> {
        let mut tokens = vec![self.bos];
        for ch in doc.chars() {
            if let Some(id) = self.encode_char(ch) {
                tokens.push(id);
            }
        }
        tokens.push(self.bos);
        tokens
    }
}

/// A dataset of documents loaded from a text file (one per line).
pub struct Dataset {
    pub docs: Vec<String>,
    pub tokenizer: Tokenizer,
}

impl Dataset {
    pub fn load(path: &Path) -> Result<Self, String> {
        let content =
            fs::read_to_string(path).map_err(|e| format!("failed to read {}: {e}", path.display()))?;
        let docs: Vec<String> = content
            .lines()
            .map(|l| l.trim().to_string())
            .filter(|l| !l.is_empty())
            .collect();
        if docs.is_empty() {
            return Err("dataset is empty".to_string());
        }
        let corpus: String = docs.join("");
        let tokenizer = Tokenizer::from_corpus(&corpus);
        Ok(Dataset { docs, tokenizer })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tokenizer_roundtrip() {
        let tok = Tokenizer::from_corpus("hello");
        assert_eq!(tok.vocab_size, 5); // e, h, l, o + BOS
        let encoded = tok.encode_doc("hello");
        assert_eq!(encoded[0], tok.bos);
        assert_eq!(*encoded.last().unwrap(), tok.bos);
        let decoded: String = encoded[1..encoded.len() - 1]
            .iter()
            .filter_map(|&id| tok.decode(id))
            .collect();
        assert_eq!(decoded, "hello");
    }
}
