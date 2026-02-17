use std::fs;
use std::path::Path;

use serde::{Deserialize, Serialize};

/// Special token IDs for chat-style conversations.
///
/// These are assigned IDs after the character tokens:
///   chars[0..n]  →  character tokens
///   n            →  user
///   n+1          →  assistant
///   n+2          →  end_turn
///   n+3          →  bos
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct SpecialTokens {
    pub user: usize,
    pub assistant: usize,
    pub end_turn: usize,
}

/// Character-level tokenizer that maps unique characters to integer IDs.
///
/// Mirrors the Python gist's tokenizer: sorted unique characters plus a
/// special Beginning-of-Sequence (BOS) token at `vocab_size - 1`.
///
/// When `special_tokens` is set, additional IDs are reserved for
/// user/assistant/end_turn tokens between the character IDs and BOS.
pub struct Tokenizer {
    pub chars: Vec<char>,
    pub bos: usize,
    pub vocab_size: usize,
    pub special_tokens: Option<SpecialTokens>,
}

impl Tokenizer {
    /// Build a tokenizer from a text corpus (no chat tokens).
    pub fn from_corpus(text: &str) -> Self {
        let mut chars: Vec<char> = text
            .chars()
            .collect::<std::collections::BTreeSet<_>>()
            .into_iter()
            .collect();
        chars.sort();
        let bos = chars.len();
        let vocab_size = chars.len() + 1;
        Tokenizer {
            chars,
            bos,
            vocab_size,
            special_tokens: None,
        }
    }

    /// Build a tokenizer from a text corpus with chat special tokens.
    pub fn from_corpus_with_chat(text: &str) -> Self {
        let mut chars: Vec<char> = text
            .chars()
            .collect::<std::collections::BTreeSet<_>>()
            .into_iter()
            .collect();
        chars.sort();
        let n = chars.len();
        let special = SpecialTokens {
            user: n,
            assistant: n + 1,
            end_turn: n + 2,
        };
        let bos = n + 3;
        let vocab_size = n + 4;
        Tokenizer {
            chars,
            bos,
            vocab_size,
            special_tokens: Some(special),
        }
    }

    /// Reconstruct a tokenizer from saved metadata.
    ///
    /// If `special_token_names` is Some, chat tokens are enabled.
    pub fn from_meta(chars: Vec<char>, special_token_names: Option<&[String]>) -> Self {
        let n = chars.len();
        if special_token_names.is_some() {
            let special = SpecialTokens {
                user: n,
                assistant: n + 1,
                end_turn: n + 2,
            };
            Tokenizer {
                chars,
                bos: n + 3,
                vocab_size: n + 4,
                special_tokens: Some(special),
            }
        } else {
            Tokenizer {
                chars,
                bos: n,
                vocab_size: n + 1,
                special_tokens: None,
            }
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

    /// Encode a string as a sequence of character token IDs.
    /// Unknown characters are silently skipped.
    pub fn encode_str(&self, s: &str) -> Vec<usize> {
        s.chars().filter_map(|ch| self.encode_char(ch)).collect()
    }

    /// Decode a sequence of token IDs into a string.
    /// Non-character tokens (special tokens, BOS) are skipped.
    pub fn decode_str(&self, tokens: &[usize]) -> String {
        tokens.iter().filter_map(|&id| self.decode(id)).collect()
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

    /// Encode a single conversation turn: `[role_token, ...chars..., end_turn]`.
    ///
    /// `role` should be `"user"` or `"assistant"`.
    /// Panics if this tokenizer has no special tokens.
    pub fn encode_turn(&self, role: &str, text: &str) -> Vec<usize> {
        let special = self
            .special_tokens
            .as_ref()
            .expect("encode_turn requires a tokenizer with chat special tokens");
        let role_token = match role {
            "user" => special.user,
            "assistant" => special.assistant,
            _ => panic!("unknown role: {role}"),
        };
        let mut tokens = vec![role_token];
        tokens.extend(self.encode_str(text));
        tokens.push(special.end_turn);
        tokens
    }

    /// Encode a full conversation (list of (role, text) pairs) into a flat
    /// token sequence.
    pub fn encode_conversation(&self, turns: &[(&str, &str)]) -> Vec<usize> {
        let mut tokens = Vec::new();
        for &(role, text) in turns {
            tokens.extend(self.encode_turn(role, text));
        }
        tokens
    }

    /// Truncate a chat prompt to fit within `block_size`, reserving 1/4 of the
    /// context window for generation and snapping to the nearest turn boundary.
    ///
    /// Returns the number of tokens dropped (0 if no truncation was needed).
    /// This is a no-op if the tokenizer has no special tokens or the prompt
    /// already fits.
    pub fn truncate_chat_prompt(&self, tokens: &mut Vec<usize>, block_size: usize) -> usize {
        let end_turn = match &self.special_tokens {
            Some(s) => s.end_turn,
            None => return 0,
        };
        let max_gen = block_size / 4;
        let max_prompt = block_size.saturating_sub(max_gen);
        if tokens.len() > max_prompt {
            let target_start = tokens.len() - max_prompt;
            let truncate_at = tokens[target_start..]
                .iter()
                .position(|&t| t == end_turn)
                .map(|i| target_start + i + 1)
                .unwrap_or(target_start);
            tokens.drain(..truncate_at);
            truncate_at
        } else {
            0
        }
    }

    /// Returns the names of special tokens (for serialization into ModelMeta).
    pub fn special_token_names(&self) -> Option<Vec<String>> {
        self.special_tokens.as_ref().map(|_| {
            vec![
                "user".to_string(),
                "assistant".to_string(),
                "end_turn".to_string(),
            ]
        })
    }
}

/// A dataset of documents loaded from a text file (one per line).
pub struct Dataset {
    pub docs: Vec<String>,
    pub tokenizer: Tokenizer,
}

impl Dataset {
    pub fn load(path: &Path) -> Result<Self, String> {
        let content = fs::read_to_string(path)
            .map_err(|e| format!("failed to read {}: {e}", path.display()))?;
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

    /// Load a dataset with chat-capable tokenizer.
    pub fn load_with_chat(path: &Path) -> Result<Self, String> {
        let content = fs::read_to_string(path)
            .map_err(|e| format!("failed to read {}: {e}", path.display()))?;
        let docs: Vec<String> = content
            .lines()
            .map(|l| l.trim().to_string())
            .filter(|l| !l.is_empty())
            .collect();
        if docs.is_empty() {
            return Err("dataset is empty".to_string());
        }
        let corpus: String = docs.join("");
        let tokenizer = Tokenizer::from_corpus_with_chat(&corpus);
        Ok(Dataset { docs, tokenizer })
    }
}

/// A single message in a conversation (for JSONL deserialization).
#[derive(Deserialize)]
pub struct ChatMessage {
    pub role: String,
    pub content: String,
}

/// A dataset of conversations loaded from a JSONL file.
///
/// Each line is a JSON array of `{"role": "user"|"assistant", "content": "..."}`.
/// Example:
/// ```text
/// [{"role":"user","content":"hello"},{"role":"assistant","content":"hi there"}]
/// [{"role":"user","content":"bye"},{"role":"assistant","content":"goodbye"}]
/// ```
pub struct ChatDataset {
    pub conversations: Vec<Vec<ChatMessage>>,
    pub tokenizer: Tokenizer,
}

impl ChatDataset {
    /// Load a chat dataset from a JSONL file.
    pub fn load(path: &Path) -> Result<Self, String> {
        let content = fs::read_to_string(path)
            .map_err(|e| format!("failed to read {}: {e}", path.display()))?;

        let mut conversations = Vec::new();
        for (i, line) in content.lines().enumerate() {
            let line = line.trim();
            if line.is_empty() {
                continue;
            }
            let msgs: Vec<ChatMessage> = serde_json::from_str(line)
                .map_err(|e| format!("invalid JSON on line {}: {e}", i + 1))?;
            if msgs.is_empty() {
                continue;
            }
            for msg in &msgs {
                if msg.role != "user" && msg.role != "assistant" {
                    return Err(format!(
                        "line {}: unknown role {:?} (expected \"user\" or \"assistant\")",
                        i + 1,
                        msg.role
                    ));
                }
            }
            conversations.push(msgs);
        }

        if conversations.is_empty() {
            return Err("chat dataset is empty".to_string());
        }

        // Build tokenizer from all message content.
        let corpus: String = conversations
            .iter()
            .flat_map(|conv| conv.iter().map(|m| m.content.as_str()))
            .collect::<Vec<_>>()
            .join("");
        let tokenizer = Tokenizer::from_corpus_with_chat(&corpus);

        Ok(ChatDataset {
            conversations,
            tokenizer,
        })
    }

    /// Encode a conversation as a training document: `[BOS, ...turns..., BOS]`.
    pub fn encode_conversation(&self, index: usize) -> Vec<usize> {
        let conv = &self.conversations[index];
        let turns: Vec<(&str, &str)> = conv
            .iter()
            .map(|m| (m.role.as_str(), m.content.as_str()))
            .collect();
        let mut tokens = vec![self.tokenizer.bos];
        tokens.extend(self.tokenizer.encode_conversation(&turns));
        tokens.push(self.tokenizer.bos);
        tokens
    }

    /// Number of conversations.
    pub fn len(&self) -> usize {
        self.conversations.len()
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

    #[test]
    fn encode_str_basic() {
        let tok = Tokenizer::from_corpus("abcde");
        let ids = tok.encode_str("bad");
        assert_eq!(ids.len(), 3);
        // chars are sorted: a=0, b=1, c=2, d=3, e=4
        assert_eq!(ids, vec![1, 0, 3]);
    }

    #[test]
    fn encode_str_skips_unknown_chars() {
        let tok = Tokenizer::from_corpus("abc");
        let ids = tok.encode_str("axbzc");
        // x and z are not in the vocabulary, should be skipped
        assert_eq!(ids, vec![0, 1, 2]);
    }

    #[test]
    fn decode_str_basic() {
        let tok = Tokenizer::from_corpus("abcde");
        let text = tok.decode_str(&[1, 0, 3]); // b, a, d
        assert_eq!(text, "bad");
    }

    #[test]
    fn decode_str_skips_special_tokens() {
        let tok = Tokenizer::from_corpus_with_chat("abc");
        let special = tok.special_tokens.as_ref().unwrap();
        let text = tok.decode_str(&[special.user, 0, 1, special.end_turn]);
        assert_eq!(text, "ab");
    }

    #[test]
    fn chat_tokenizer_vocab_size() {
        let tok = Tokenizer::from_corpus_with_chat("hello");
        // e, h, l, o = 4 chars + user + assistant + end_turn + BOS = 8
        assert_eq!(tok.vocab_size, 8);
        assert_eq!(tok.chars.len(), 4);

        let special = tok.special_tokens.as_ref().unwrap();
        assert_eq!(special.user, 4);
        assert_eq!(special.assistant, 5);
        assert_eq!(special.end_turn, 6);
        assert_eq!(tok.bos, 7);
    }

    #[test]
    fn encode_turn_user() {
        let tok = Tokenizer::from_corpus_with_chat("helo");
        let special = tok.special_tokens.as_ref().unwrap();

        let tokens = tok.encode_turn("user", "hello");
        // [user, h, e, l, l, o, end_turn]
        assert_eq!(tokens[0], special.user);
        assert_eq!(*tokens.last().unwrap(), special.end_turn);
        assert_eq!(tok.decode_str(&tokens), "hello");
    }

    #[test]
    fn encode_turn_assistant() {
        let tok = Tokenizer::from_corpus_with_chat("hi");
        let special = tok.special_tokens.as_ref().unwrap();

        let tokens = tok.encode_turn("assistant", "hi");
        assert_eq!(tokens[0], special.assistant);
        assert_eq!(*tokens.last().unwrap(), special.end_turn);
        assert_eq!(tok.decode_str(&tokens), "hi");
    }

    #[test]
    fn encode_conversation_roundtrip() {
        let tok = Tokenizer::from_corpus_with_chat("aehiloptwy ?");
        let special = tok.special_tokens.as_ref().unwrap();

        let turns = vec![("user", "hey"), ("assistant", "hello"), ("user", "what")];
        let tokens = tok.encode_conversation(&turns);

        // Should be: [user, h,e,y, end_turn, assistant, h,e,l,l,o, end_turn, user, w,h,a,t, end_turn]
        assert_eq!(tokens[0], special.user);
        let mut end_turn_count = 0;
        for &t in &tokens {
            if t == special.end_turn {
                end_turn_count += 1;
            }
        }
        assert_eq!(end_turn_count, 3);

        // Full decoded text (skipping special tokens) should be the concatenation
        assert_eq!(tok.decode_str(&tokens), "heyhellowhat");
    }

    #[test]
    fn from_meta_without_chat() {
        let tok = Tokenizer::from_meta(vec!['a', 'b', 'c'], None);
        assert_eq!(tok.vocab_size, 4); // 3 chars + BOS
        assert_eq!(tok.bos, 3);
        assert!(tok.special_tokens.is_none());
    }

    #[test]
    fn from_meta_with_chat() {
        let names = vec![
            "user".to_string(),
            "assistant".to_string(),
            "end_turn".to_string(),
        ];
        let tok = Tokenizer::from_meta(vec!['a', 'b', 'c'], Some(&names));
        assert_eq!(tok.vocab_size, 7); // 3 chars + user + assistant + end_turn + BOS
        assert_eq!(tok.bos, 6);
        let special = tok.special_tokens.as_ref().unwrap();
        assert_eq!(special.user, 3);
        assert_eq!(special.assistant, 4);
        assert_eq!(special.end_turn, 5);
    }

    #[test]
    fn special_token_names_roundtrip() {
        let tok = Tokenizer::from_corpus_with_chat("abc");
        let names = tok.special_token_names();
        assert!(names.is_some());
        let names = names.unwrap();
        assert_eq!(names, vec!["user", "assistant", "end_turn"]);

        // Reconstruct from meta
        let tok2 = Tokenizer::from_meta(tok.chars.clone(), Some(&names));
        assert_eq!(tok2.vocab_size, tok.vocab_size);
        assert_eq!(tok2.bos, tok.bos);
    }

    #[test]
    #[should_panic(expected = "encode_turn requires")]
    fn encode_turn_panics_without_chat() {
        let tok = Tokenizer::from_corpus("hello");
        tok.encode_turn("user", "test");
    }

    #[test]
    #[should_panic(expected = "unknown role")]
    fn encode_turn_panics_bad_role() {
        let tok = Tokenizer::from_corpus_with_chat("hello");
        tok.encode_turn("system", "test");
    }

    // --- ChatDataset tests ---

    fn write_temp_file(content: &str) -> std::path::PathBuf {
        let dir = std::env::temp_dir().join("microgpt_test");
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join(format!("test_{}.jsonl", content.len()));
        std::fs::write(&path, content).unwrap();
        path
    }

    #[test]
    fn chat_dataset_load_basic() {
        let data = r#"[{"role":"user","content":"hello"},{"role":"assistant","content":"hi"}]
[{"role":"user","content":"bye"},{"role":"assistant","content":"later"}]"#;
        let path = write_temp_file(data);
        let ds = ChatDataset::load(&path).unwrap();
        assert_eq!(ds.len(), 2);
        assert_eq!(ds.conversations[0].len(), 2);
        assert_eq!(ds.conversations[0][0].role, "user");
        assert_eq!(ds.conversations[0][0].content, "hello");
        assert!(ds.tokenizer.special_tokens.is_some());
    }

    #[test]
    fn chat_dataset_encode_conversation() {
        let data = r#"[{"role":"user","content":"hi"},{"role":"assistant","content":"yo"}]"#;
        let path = write_temp_file(data);
        let ds = ChatDataset::load(&path).unwrap();

        let tokens = ds.encode_conversation(0);
        let bos = ds.tokenizer.bos;
        let special = ds.tokenizer.special_tokens.as_ref().unwrap();

        // Should be [BOS, user, h, i, end_turn, assistant, y, o, end_turn, BOS]
        assert_eq!(tokens[0], bos);
        assert_eq!(tokens[1], special.user);
        assert_eq!(*tokens.last().unwrap(), bos);

        // Decoded text should be "hiyo"
        assert_eq!(ds.tokenizer.decode_str(&tokens), "hiyo");
    }

    #[test]
    fn chat_dataset_skips_empty_lines() {
        let data = r#"[{"role":"user","content":"a"},{"role":"assistant","content":"b"}]

[{"role":"user","content":"c"},{"role":"assistant","content":"d"}]
"#;
        let path = write_temp_file(data);
        let ds = ChatDataset::load(&path).unwrap();
        assert_eq!(ds.len(), 2);
    }

    #[test]
    fn chat_dataset_rejects_bad_role() {
        let data = r#"[{"role":"system","content":"you are helpful"}]"#;
        let path = write_temp_file(data);
        let err = ChatDataset::load(&path).err().unwrap();
        assert!(err.contains("unknown role"));
    }

    #[test]
    fn chat_dataset_rejects_bad_json() {
        let data = "not json at all";
        let path = write_temp_file(data);
        let err = ChatDataset::load(&path).err().unwrap();
        assert!(err.contains("invalid JSON"));
    }

    #[test]
    fn chat_dataset_rejects_empty() {
        let data = "\n\n";
        let path = write_temp_file(data);
        let err = ChatDataset::load(&path).err().unwrap();
        assert!(err.contains("empty"));
    }

    #[test]
    fn chat_dataset_tokenizer_covers_all_content() {
        let data = r#"[{"role":"user","content":"xyz"},{"role":"assistant","content":"abc"}]"#;
        let path = write_temp_file(data);
        let ds = ChatDataset::load(&path).unwrap();

        // All characters from the content should be encodable.
        for ch in "abcxyz".chars() {
            assert!(
                ds.tokenizer.encode_char(ch).is_some(),
                "char {ch:?} should be in vocabulary"
            );
        }
    }
}
