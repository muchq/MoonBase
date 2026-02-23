use std::fs;
use std::path::Path;

use serde::{Deserialize, Serialize};
use tokenizers::models::bpe::{BpeTrainer, BPE};
use tokenizers::pre_tokenizers::byte_level::ByteLevel;
use tokenizers::AddedToken;

/// Special token IDs for chat-style conversations.
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct SpecialTokens {
    pub user: usize,
    pub assistant: usize,
    pub end_turn: usize,
}

const BOS_TOKEN: &str = "<bos>";
const USER_TOKEN: &str = "<user>";
const ASSISTANT_TOKEN: &str = "<assistant>";
const END_TURN_TOKEN: &str = "<end_turn>";

/// BPE tokenizer wrapping the HuggingFace `tokenizers` crate.
pub struct Tokenizer {
    inner: tokenizers::Tokenizer,
    pub bos: usize,
    pub vocab_size: usize,
    pub special_tokens: Option<SpecialTokens>,
}

impl Tokenizer {
    /// Train a BPE tokenizer on the given corpus text.
    ///
    /// If `chat` is true, special tokens for user/assistant/end_turn are added
    /// alongside the BOS token.
    pub fn train_bpe(corpus: &str, vocab_size: usize, chat: bool) -> Self {
        let special_strs: Vec<&str> = if chat {
            vec![BOS_TOKEN, USER_TOKEN, ASSISTANT_TOKEN, END_TURN_TOKEN]
        } else {
            vec![BOS_TOKEN]
        };

        let special_tokens: Vec<AddedToken> = special_strs
            .iter()
            .map(|s| AddedToken::from(*s, true))
            .collect();

        let mut trainer = BpeTrainer::builder()
            .vocab_size(vocab_size)
            .special_tokens(special_tokens)
            .show_progress(false)
            .build();

        // Use the concrete-typed TokenizerImpl so the Trainer<Model=BPE>
        // bound is satisfied, then convert to the type-erased Tokenizer.
        type TypedTokenizer = tokenizers::TokenizerImpl<
            BPE,
            tokenizers::NormalizerWrapper,
            tokenizers::PreTokenizerWrapper,
            tokenizers::PostProcessorWrapper,
            tokenizers::DecoderWrapper,
        >;
        let mut typed = TypedTokenizer::new(BPE::default());
        let pre_tok = ByteLevel::default().add_prefix_space(false);
        typed.with_pre_tokenizer(Some(pre_tok));
        typed.with_decoder(Some(
            tokenizers::decoders::byte_level::ByteLevel::default(),
        ));

        let tmp_dir = std::env::temp_dir().join("microgpt_bpe_train");
        fs::create_dir_all(&tmp_dir).expect("failed to create temp dir");
        let unique = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let tmp_path = tmp_dir.join(format!("corpus_{unique}_{}.txt", std::process::id()));
        fs::write(&tmp_path, corpus).expect("failed to write corpus to temp file");

        typed
            .train_from_files(
                &mut trainer,
                vec![tmp_path.to_string_lossy().to_string()],
            )
            .expect("BPE training should not fail on valid text");

        let _ = fs::remove_file(&tmp_path);

        let tokenizer: tokenizers::Tokenizer = typed.into();

        Self::from_hf(tokenizer, chat)
    }

    /// Load a tokenizer from a `tokenizer.json` file (HuggingFace format).
    pub fn from_file(path: &Path) -> Result<Self, String> {
        let inner = tokenizers::Tokenizer::from_file(path)
            .map_err(|e| format!("failed to load tokenizer: {e}"))?;

        let has_chat = inner.token_to_id(USER_TOKEN).is_some();
        Ok(Self::from_hf(inner, has_chat))
    }

    /// Save the tokenizer to a `tokenizer.json` file.
    pub fn save(&self, path: &Path) -> Result<(), String> {
        self.inner
            .save(path, false)
            .map_err(|e| format!("failed to save tokenizer: {e}"))
    }

    fn from_hf(inner: tokenizers::Tokenizer, chat: bool) -> Self {
        let bos = inner
            .token_to_id(BOS_TOKEN)
            .expect("tokenizer must have <bos> token") as usize;

        let vocab_size = inner.get_vocab_size(true);

        let special_tokens = if chat {
            Some(SpecialTokens {
                user: inner
                    .token_to_id(USER_TOKEN)
                    .expect("chat tokenizer must have <user>") as usize,
                assistant: inner
                    .token_to_id(ASSISTANT_TOKEN)
                    .expect("chat tokenizer must have <assistant>") as usize,
                end_turn: inner
                    .token_to_id(END_TURN_TOKEN)
                    .expect("chat tokenizer must have <end_turn>") as usize,
            })
        } else {
            None
        };

        Tokenizer {
            inner,
            bos,
            vocab_size,
            special_tokens,
        }
    }

    /// Decode a single token ID to its string representation.
    pub fn decode(&self, id: usize) -> Option<String> {
        self.inner
            .decode(&[id as u32], false)
            .ok()
            .filter(|s| !s.is_empty())
    }

    /// Encode a string into a sequence of token IDs.
    pub fn encode_str(&self, s: &str) -> Vec<usize> {
        let encoding = self
            .inner
            .encode(s, false)
            .expect("BPE encoding should not fail");
        encoding.get_ids().iter().map(|&id| id as usize).collect()
    }

    /// Decode a sequence of token IDs into a string.
    pub fn decode_str(&self, tokens: &[usize]) -> String {
        let ids: Vec<u32> = tokens.iter().map(|&t| t as u32).collect();
        self.inner.decode(&ids, true).unwrap_or_default()
    }

    /// Encode a document as `[BOS, ...tokens..., BOS]`.
    pub fn encode_doc(&self, doc: &str) -> Vec<usize> {
        let mut tokens = vec![self.bos];
        tokens.extend(self.encode_str(doc));
        tokens.push(self.bos);
        tokens
    }

    /// Encode a single conversation turn: `[role_token, ...tokens..., end_turn]`.
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
    pub tokenized_docs: Vec<Vec<usize>>,
    pub tokenizer: Tokenizer,
}

impl Dataset {
    pub fn load(path: &Path, vocab_size: usize) -> Result<Self, String> {
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
        let corpus: String = docs.join("\n");
        let tokenizer = Tokenizer::train_bpe(&corpus, vocab_size, false);
        let tokenized_docs = docs.iter().map(|d| tokenizer.encode_doc(d)).collect();
        Ok(Dataset {
            docs,
            tokenized_docs,
            tokenizer,
        })
    }

    /// Remove documents whose token length exceeds `block_size + 1`.
    /// Returns the number of documents removed.
    pub fn filter_to_block_size(&mut self, block_size: usize) -> usize {
        let before = self.docs.len();
        let keep: Vec<bool> = self.tokenized_docs.iter().map(|t| t.len() <= block_size + 1).collect();
        let mut i = 0;
        self.docs.retain(|_| { let k = keep[i]; i += 1; k });
        let mut j = 0;
        self.tokenized_docs.retain(|_| { let k = keep[j]; j += 1; k });
        before - self.docs.len()
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
pub struct ChatDataset {
    pub conversations: Vec<Vec<ChatMessage>>,
    pub tokenized_conversations: Vec<Vec<usize>>,
    pub tokenizer: Tokenizer,
    /// Conversations where a trailing user turn was trimmed.
    pub trimmed_count: usize,
    /// Conversations skipped entirely (no assistant turn).
    pub skipped_count: usize,
}

impl ChatDataset {
    /// Load a chat dataset from a JSONL file.
    pub fn load(path: &Path, vocab_size: usize) -> Result<Self, String> {
        let content = fs::read_to_string(path)
            .map_err(|e| format!("failed to read {}: {e}", path.display()))?;

        let mut conversations = Vec::new();
        let mut trimmed_count = 0usize;
        let mut skipped_count = 0usize;
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
            // Trim to the last assistant turn — a trailing user message
            // with no reply provides no supervision for assistant generation
            // and teaches the model to produce empty responses.
            let last_asst = msgs.iter().rposition(|m| m.role == "assistant");
            let orig_len = msgs.len();
            let msgs: Vec<ChatMessage> = match last_asst {
                Some(idx) => msgs.into_iter().take(idx + 1).collect(),
                None => {
                    skipped_count += 1;
                    continue;
                }
            };
            if msgs.len() < orig_len {
                trimmed_count += 1;
            }
            conversations.push(msgs);
        }

        if conversations.is_empty() {
            return Err("chat dataset is empty".to_string());
        }

        let corpus: String = conversations
            .iter()
            .flat_map(|conv| conv.iter().map(|m| m.content.as_str()))
            .collect::<Vec<_>>()
            .join("\n");
        let tokenizer = Tokenizer::train_bpe(&corpus, vocab_size, true);

        let mut ds = ChatDataset {
            conversations,
            tokenized_conversations: Vec::new(),
            tokenizer,
            trimmed_count,
            skipped_count,
        };
        ds.tokenized_conversations = (0..ds.len())
            .map(|i| ds.encode_conversation(i))
            .collect();

        Ok(ds)
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

    /// Remove conversations whose token length exceeds `block_size + 1`.
    /// Returns the number of conversations removed.
    pub fn filter_to_block_size(&mut self, block_size: usize) -> usize {
        let before = self.conversations.len();
        let keep: Vec<bool> = self.tokenized_conversations.iter().map(|t| t.len() <= block_size + 1).collect();
        let mut i = 0;
        self.conversations.retain(|_| { let k = keep[i]; i += 1; k });
        let mut j = 0;
        self.tokenized_conversations.retain(|_| { let k = keep[j]; j += 1; k });
        before - self.conversations.len()
    }

    /// Number of conversations.
    pub fn len(&self) -> usize {
        self.conversations.len()
    }

    pub fn is_empty(&self) -> bool {
        self.conversations.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_CORPUS: &str = "Hello world! This is a test of the byte-level BPE tokenizer. \
        It should handle punctuation, numbers like 42, and various words properly.";

    fn train_text_tokenizer() -> Tokenizer {
        Tokenizer::train_bpe(TEST_CORPUS, 500, false)
    }

    fn train_chat_tokenizer() -> Tokenizer {
        Tokenizer::train_bpe(TEST_CORPUS, 500, true)
    }

    #[test]
    fn bpe_tokenizer_roundtrip() {
        let tok = train_text_tokenizer();
        let ids = tok.encode_str("hello");
        assert!(!ids.is_empty());
        let decoded = tok.decode_str(&ids);
        assert_eq!(decoded, "hello");
    }

    #[test]
    fn bpe_encode_doc_has_bos_bookends() {
        let tok = train_text_tokenizer();
        let doc = tok.encode_doc("hello");
        assert_eq!(doc[0], tok.bos);
        assert_eq!(*doc.last().unwrap(), tok.bos);
        assert!(doc.len() >= 3);
    }

    #[test]
    fn bpe_vocab_size_is_positive() {
        let tok = train_text_tokenizer();
        assert!(tok.vocab_size > 0);
    }

    #[test]
    fn bpe_decode_single_token() {
        let tok = train_text_tokenizer();
        let ids = tok.encode_str("hello");
        for &id in &ids {
            let decoded = tok.decode(id);
            assert!(decoded.is_some(), "every valid token should decode");
        }
    }

    #[test]
    fn bpe_decode_str_skips_special_tokens() {
        let tok = train_chat_tokenizer();
        let special = tok.special_tokens.as_ref().unwrap();
        let text_ids = tok.encode_str("hello");
        let mut ids = vec![special.user];
        ids.extend(&text_ids);
        ids.push(special.end_turn);
        let decoded = tok.decode_str(&ids);
        assert!(!decoded.contains("<user>"));
        assert!(!decoded.contains("<end_turn>"));
        assert_eq!(decoded, "hello");
    }

    #[test]
    fn chat_tokenizer_has_special_tokens() {
        let tok = train_chat_tokenizer();
        assert!(tok.special_tokens.is_some());
        let special = tok.special_tokens.as_ref().unwrap();
        assert_ne!(special.user, special.assistant);
        assert_ne!(special.assistant, special.end_turn);
        assert_ne!(special.end_turn, tok.bos);
    }

    #[test]
    fn encode_turn_structure() {
        let tok = train_chat_tokenizer();
        let special = tok.special_tokens.as_ref().unwrap();

        let tokens = tok.encode_turn("user", "hello");
        assert_eq!(tokens[0], special.user);
        assert_eq!(*tokens.last().unwrap(), special.end_turn);
    }

    #[test]
    fn encode_turn_assistant() {
        let tok = train_chat_tokenizer();
        let special = tok.special_tokens.as_ref().unwrap();

        let tokens = tok.encode_turn("assistant", "hi");
        assert_eq!(tokens[0], special.assistant);
        assert_eq!(*tokens.last().unwrap(), special.end_turn);
    }

    #[test]
    fn encode_conversation_has_all_end_turns() {
        let tok = train_chat_tokenizer();
        let special = tok.special_tokens.as_ref().unwrap();

        let turns = vec![("user", "hey"), ("assistant", "hello"), ("user", "what")];
        let tokens = tok.encode_conversation(&turns);

        let end_turn_count = tokens.iter().filter(|&&t| t == special.end_turn).count();
        assert_eq!(end_turn_count, 3);
    }

    #[test]
    #[should_panic(expected = "encode_turn requires")]
    fn encode_turn_panics_without_chat() {
        let tok = train_text_tokenizer();
        tok.encode_turn("user", "test");
    }

    #[test]
    #[should_panic(expected = "unknown role")]
    fn encode_turn_panics_bad_role() {
        let tok = train_chat_tokenizer();
        tok.encode_turn("system", "test");
    }

    #[test]
    fn tokenizer_save_load_roundtrip() {
        let tok = train_chat_tokenizer();
        let dir = std::env::temp_dir().join("microgpt_bpe_test");
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("tokenizer.json");

        tok.save(&path).unwrap();
        let tok2 = Tokenizer::from_file(&path).unwrap();

        assert_eq!(tok2.vocab_size, tok.vocab_size);
        assert_eq!(tok2.bos, tok.bos);
        assert!(tok2.special_tokens.is_some());

        let ids1 = tok.encode_str("hello world");
        let ids2 = tok2.encode_str("hello world");
        assert_eq!(ids1, ids2);
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
        let ds = ChatDataset::load(&path, 256).unwrap();
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
        let ds = ChatDataset::load(&path, 256).unwrap();

        let tokens = ds.encode_conversation(0);
        let bos = ds.tokenizer.bos;
        let special = ds.tokenizer.special_tokens.as_ref().unwrap();

        assert_eq!(tokens[0], bos);
        assert_eq!(tokens[1], special.user);
        assert_eq!(*tokens.last().unwrap(), bos);
    }

    #[test]
    fn chat_dataset_skips_empty_lines() {
        let data = r#"[{"role":"user","content":"a"},{"role":"assistant","content":"b"}]

[{"role":"user","content":"c"},{"role":"assistant","content":"d"}]
"#;
        let path = write_temp_file(data);
        let ds = ChatDataset::load(&path, 256).unwrap();
        assert_eq!(ds.len(), 2);
    }

    #[test]
    fn chat_dataset_rejects_bad_role() {
        let data = r#"[{"role":"system","content":"you are helpful"}]"#;
        let path = write_temp_file(data);
        let err = ChatDataset::load(&path, 256).err().unwrap();
        assert!(err.contains("unknown role"));
    }

    #[test]
    fn chat_dataset_rejects_bad_json() {
        let data = "not json at all";
        let path = write_temp_file(data);
        let err = ChatDataset::load(&path, 256).err().unwrap();
        assert!(err.contains("invalid JSON"));
    }

    #[test]
    fn chat_dataset_rejects_empty() {
        let data = "\n\n";
        let path = write_temp_file(data);
        let err = ChatDataset::load(&path, 256).err().unwrap();
        assert!(err.contains("empty"));
    }

    #[test]
    fn chat_dataset_trims_trailing_user_turn() {
        let data = r#"[{"role":"user","content":"hi"},{"role":"assistant","content":"yo"},{"role":"user","content":"follow up"}]
[{"role":"user","content":"bye"},{"role":"assistant","content":"later"}]"#;
        let path = write_temp_file(data);
        let ds = ChatDataset::load(&path, 256).unwrap();
        assert_eq!(ds.len(), 2);
        // First conversation should be trimmed to 2 turns (trailing user dropped)
        assert_eq!(ds.conversations[0].len(), 2);
        assert_eq!(ds.conversations[0][1].role, "assistant");
        assert_eq!(ds.conversations[0][1].content, "yo");
        // Second conversation is already complete
        assert_eq!(ds.conversations[1].len(), 2);
    }

    #[test]
    fn chat_dataset_skips_user_only_conversation() {
        let data = r#"[{"role":"user","content":"hello?"}]
[{"role":"user","content":"bye"},{"role":"assistant","content":"later"}]"#;
        let path = write_temp_file(data);
        let ds = ChatDataset::load(&path, 256).unwrap();
        assert_eq!(ds.len(), 1);
        assert_eq!(ds.conversations[0][0].content, "bye");
    }

    #[test]
    fn chat_dataset_empty_after_trim() {
        let data = r#"[{"role":"user","content":"hello?"}]
[{"role":"user","content":"anyone there?"}]"#;
        let path = write_temp_file(data);
        let err = ChatDataset::load(&path, 256).err().unwrap();
        assert!(err.contains("empty"));
    }

    #[test]
    fn bpe_encode_str_roundtrips() {
        let tok = train_text_tokenizer();
        let text = "This is a test";
        let ids = tok.encode_str(text);
        let decoded = tok.decode_str(&ids);
        assert_eq!(decoded, text);
    }

    #[test]
    fn text_tokenizer_has_no_special_tokens() {
        let tok = train_text_tokenizer();
        assert!(tok.special_tokens.is_none());
    }

    #[test]
    fn chat_tokenizer_has_more_vocab_than_text() {
        let text_tok = train_text_tokenizer();
        let chat_tok = train_chat_tokenizer();
        assert!(
            chat_tok.vocab_size > text_tok.vocab_size,
            "chat tokenizer should have extra special tokens: {} vs {}",
            chat_tok.vocab_size,
            text_tok.vocab_size
        );
    }

    #[test]
    fn text_tokenizer_save_load_roundtrip() {
        let tok = train_text_tokenizer();
        let dir = std::env::temp_dir().join("microgpt_bpe_text_rt");
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("tokenizer.json");

        tok.save(&path).unwrap();
        let tok2 = Tokenizer::from_file(&path).unwrap();

        assert_eq!(tok2.vocab_size, tok.vocab_size);
        assert_eq!(tok2.bos, tok.bos);
        assert!(tok2.special_tokens.is_none());

        let ids1 = tok.encode_str("hello world");
        let ids2 = tok2.encode_str("hello world");
        assert_eq!(ids1, ids2);
    }

    #[test]
    fn from_file_returns_error_on_missing_path() {
        let bad_path = std::path::Path::new("/tmp/microgpt_does_not_exist/tokenizer.json");
        let result = Tokenizer::from_file(bad_path);
        assert!(result.is_err());
    }

    #[test]
    fn bpe_encode_doc_content_roundtrips() {
        let tok = train_text_tokenizer();
        let text = "Hello world";
        let doc = tok.encode_doc(text);
        // Strip the BOS bookends and decode the content tokens.
        let content_ids = &doc[1..doc.len() - 1];
        let decoded = tok.decode_str(content_ids);
        assert_eq!(decoded, text);
    }

    #[test]
    fn bpe_handles_unicode() {
        // Repeat each unicode string many times so the multi-byte pairs
        // have unambiguously high frequency and always get merged,
        // regardless of BPE tie-breaking order.
        let words = ["café", "naïve", "résumé", "日本語", "🎉", "🚀"];
        let corpus: String = words
            .iter()
            .flat_map(|w| std::iter::repeat(*w).take(50))
            .collect::<Vec<_>>()
            .join(" ");
        let tok = Tokenizer::train_bpe(&corpus, 500, false);
        for text in &words {
            let ids = tok.encode_str(text);
            assert!(!ids.is_empty());
            let decoded = tok.decode_str(&ids);
            assert_eq!(&decoded, *text, "roundtrip failed for {text}");
        }
    }

    #[test]
    fn bpe_handles_empty_string() {
        let tok = train_text_tokenizer();
        let ids = tok.encode_str("");
        assert!(ids.is_empty());
        let decoded = tok.decode_str(&ids);
        assert!(decoded.is_empty());
    }

    #[test]
    fn bpe_vocab_size_does_not_exceed_requested() {
        let tok = Tokenizer::train_bpe(TEST_CORPUS, 300, false);
        assert!(
            tok.vocab_size <= 300,
            "vocab should be <= requested 300, got {}",
            tok.vocab_size
        );
        assert!(
            tok.vocab_size > 0,
            "vocab should be positive, got {}",
            tok.vocab_size
        );
    }

    #[test]
    fn special_token_names_chat() {
        let tok = train_chat_tokenizer();
        let names = tok.special_token_names();
        assert!(names.is_some());
        assert_eq!(
            names.unwrap(),
            vec!["user", "assistant", "end_turn"]
        );
    }

    #[test]
    fn special_token_names_text() {
        let tok = train_text_tokenizer();
        assert!(tok.special_token_names().is_none());
    }

    #[test]
    fn truncate_chat_prompt_no_op_when_short() {
        let tok = train_chat_tokenizer();
        let mut tokens = vec![tok.bos, 1, 2, 3];
        let dropped = tok.truncate_chat_prompt(&mut tokens, 256);
        assert_eq!(dropped, 0);
        assert_eq!(tokens.len(), 4);
    }

    #[test]
    fn truncate_chat_prompt_drops_early_tokens() {
        let tok = train_chat_tokenizer();
        let special = tok.special_tokens.as_ref().unwrap();
        // Build a prompt that exceeds block_size=16
        let mut tokens = Vec::new();
        for _ in 0..5 {
            tokens.push(special.user);
            tokens.extend(tok.encode_str("hi"));
            tokens.push(special.end_turn);
        }
        let original_len = tokens.len();
        let dropped = tok.truncate_chat_prompt(&mut tokens, 16);
        assert!(dropped > 0, "should have truncated");
        assert!(
            tokens.len() < original_len,
            "tokens should be shorter after truncation"
        );
        // Remaining tokens should fit within the max prompt budget (3/4 of block_size)
        assert!(tokens.len() <= 12);
    }

    #[test]
    fn truncate_chat_prompt_strips_bos() {
        let tok = train_chat_tokenizer();
        let special = tok.special_tokens.as_ref().unwrap();
        // Build a history that starts with BOS and exceeds block_size=16
        let mut tokens = vec![tok.bos];
        for _ in 0..5 {
            tokens.push(special.user);
            tokens.extend(tok.encode_str("hi"));
            tokens.push(special.end_turn);
        }
        assert_eq!(tokens[0], tok.bos);
        let dropped = tok.truncate_chat_prompt(&mut tokens, 16);
        assert!(dropped > 0);
        // After truncation, BOS should be gone (it was in the dropped prefix)
        assert_ne!(
            tokens.first().copied(),
            Some(tok.bos),
            "truncation should have removed the leading BOS"
        );
    }

    #[test]
    fn truncate_chat_prompt_no_op_without_chat() {
        let tok = train_text_tokenizer();
        let mut tokens = vec![0; 100];
        let dropped = tok.truncate_chat_prompt(&mut tokens, 16);
        assert_eq!(dropped, 0);
        assert_eq!(tokens.len(), 100);
    }

    #[test]
    fn encode_conversation_content_roundtrips() {
        let tok = train_chat_tokenizer();
        let special = tok.special_tokens.as_ref().unwrap();
        let turns = vec![("user", "hello"), ("assistant", "world")];
        let tokens = tok.encode_conversation(&turns);

        // Collect content tokens by stripping role and end_turn markers.
        let content_tokens: Vec<usize> = tokens
            .iter()
            .copied()
            .filter(|&t| t != special.user && t != special.assistant && t != special.end_turn)
            .collect();
        let decoded = tok.decode_str(&content_tokens);
        assert_eq!(decoded, "helloworld");
    }

    #[test]
    fn dataset_load_text_mode() {
        let content = "line one\nline two\nline three\n";
        let dir = std::env::temp_dir().join("microgpt_ds_test");
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("test_text.txt");
        std::fs::write(&path, content).unwrap();

        let ds = Dataset::load(&path, 300).unwrap();
        assert_eq!(ds.docs.len(), 3);
        assert!(ds.tokenizer.special_tokens.is_none());
        assert!(!ds.tokenized_docs.is_empty());
        for doc in &ds.tokenized_docs {
            assert_eq!(doc[0], ds.tokenizer.bos);
            assert_eq!(*doc.last().unwrap(), ds.tokenizer.bos);
        }
    }

    #[test]
    fn dataset_filter_to_block_size_removes_long_docs() {
        let content = "short\na]somewhat longer line with more tokens in it for testing purposes\nhi\n";
        let dir = std::env::temp_dir().join("microgpt_filter_test");
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("test_filter.txt");
        std::fs::write(&path, content).unwrap();

        let mut ds = Dataset::load(&path, 300).unwrap();
        let original_len = ds.docs.len();
        assert_eq!(original_len, 3);

        // Use a very small block_size so some docs are filtered
        let removed = ds.filter_to_block_size(4);
        assert!(removed > 0, "should have removed at least one doc");
        assert_eq!(ds.docs.len() + removed, original_len);
        assert_eq!(ds.tokenized_docs.len(), ds.docs.len());
        // All remaining docs should fit
        for doc in &ds.tokenized_docs {
            assert!(doc.len() <= 5, "remaining docs should fit in block_size+1");
        }
    }

    #[test]
    fn dataset_filter_to_block_size_no_op_when_all_fit() {
        let content = "a\nb\nc\n";
        let dir = std::env::temp_dir().join("microgpt_filter_noop");
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("test_noop.txt");
        std::fs::write(&path, content).unwrap();

        let mut ds = Dataset::load(&path, 300).unwrap();
        let removed = ds.filter_to_block_size(1024);
        assert_eq!(removed, 0);
        assert_eq!(ds.docs.len(), 3);
    }

    #[test]
    fn chat_dataset_filter_to_block_size_removes_long_convos() {
        let short = r#"[{"role":"user","content":"hi"},{"role":"assistant","content":"yo"}]"#;
        let long = r#"[{"role":"user","content":"tell me a very long story about dragons and knights and castles and princesses and wizards and all sorts of magical creatures that live in an enchanted forest far far away"},{"role":"assistant","content":"once upon a time in a land far far away there lived a dragon who was friends with a knight and they went on many adventures together exploring caves and mountains"}]"#;
        let data = format!("{short}\n{long}\n{short}\n");
        let path = write_temp_file(&data);

        let mut ds = ChatDataset::load(&path, 256).unwrap();
        assert_eq!(ds.len(), 3);

        // Small block_size to force the long conversation out
        let removed = ds.filter_to_block_size(20);
        assert!(removed > 0, "should have removed the long conversation");
        assert_eq!(ds.len() + removed, 3);
        assert_eq!(ds.tokenized_conversations.len(), ds.len());
        for conv in &ds.tokenized_conversations {
            assert!(conv.len() <= 21);
        }
    }

    #[test]
    fn chat_dataset_filter_to_block_size_no_op_when_all_fit() {
        let data = r#"[{"role":"user","content":"hi"},{"role":"assistant","content":"yo"}]
[{"role":"user","content":"bye"},{"role":"assistant","content":"later"}]"#;
        let path = write_temp_file(data);

        let mut ds = ChatDataset::load(&path, 256).unwrap();
        let removed = ds.filter_to_block_size(1024);
        assert_eq!(removed, 0);
        assert_eq!(ds.len(), 2);
    }
}
