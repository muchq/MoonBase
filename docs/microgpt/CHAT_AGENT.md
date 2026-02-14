# microgpt chat agent

Add multi-turn chat capabilities to microgpt: special tokens for
conversation turns, prompt-conditioned generation, an interactive CLI,
and an HTTP chat endpoint.

## Motivation

The current microgpt is stateless and single-turn. Each `generate` call
starts from BOS and produces an independent sample. To support a chat
agent (even a tiny, educational one) we need:

1. A way to encode conversation turns (user/assistant) as token sequences.
2. A way to "prefill" the model with a prompt and then sample the continuation.
3. An interactive REPL that maintains conversation history.
4. An HTTP endpoint that accepts a messages array and returns a completion.

## Design

### 1. Configurable model dimensions (medium)

The current model constants (`N_EMBD=16`, `N_HEAD=4`, `N_LAYER=1`,
`BLOCK_SIZE=16`) are compile-time. A chat model needs much larger
dimensions, and different checkpoints should be able to use different
sizes.

**Changes to `model.rs`:**

```rust
/// Runtime-configurable model hyperparameters.
#[derive(Clone, Copy, Debug, Serialize, Deserialize)]
pub struct ModelConfig {
    pub n_embd: usize,
    pub n_head: usize,
    pub n_layer: usize,
    pub block_size: usize,
}
```

- `Gpt` and `InferenceGpt` gain a `config: ModelConfig` field.
- `KvCache::new(config)` and `InferenceKvCache::new(config)` take config.
- `head_dim()` is a method on `ModelConfig`.
- The existing constants remain as defaults.
- `ModelMeta` already carries these values; we derive `ModelConfig` from it.

### 2. Special tokens (easy)

Reserve token IDs for conversation roles after the character IDs:

```
IDs 0..chars.len()         → character tokens
chars.len()                → <user>
chars.len() + 1            → <assistant>
chars.len() + 2            → <end_turn>
chars.len() + 3            → <bos>
vocab_size = chars.len() + 4
```

**Changes to `data.rs`:**

```rust
pub struct SpecialTokens {
    pub user: usize,
    pub assistant: usize,
    pub end_turn: usize,
}
```

- `Tokenizer` gains `special_tokens: Option<SpecialTokens>`.
- `Tokenizer::from_corpus_with_chat()` builds the extended vocab.
- `Tokenizer::encode_str(s)` encodes a string (without BOS wrapping).
- `Tokenizer::encode_turn(role, text)` encodes
  `[role_token, ...chars..., end_turn]`.
- `ModelMeta` gains `special_tokens: Option<Vec<String>>` for
  persistence. Old models without this field load fine (defaults to None).

A conversation is encoded as:
```
<user> h e l l o <end_turn> <assistant> h i <end_turn>
```

### 3. Prompt-conditioned generation (easy)

**New method on `InferenceGpt`:**

```rust
pub fn generate_from_prompt(
    &self,
    prompt_tokens: &[usize],
    stop_token: usize,
    temperature: f64,
    rng_seed: u64,
    on_token: impl FnMut(usize),
) -> Vec<usize>
```

1. **Prefill:** run `forward()` for each prompt token to build the KV
   cache. Logits are discarded except for the last prompt position.
2. **Decode:** sample tokens one at a time, calling `on_token` for
   streaming, until `stop_token` is emitted or `block_size` is reached.

### 4. Chat REPL (easy, CLI)

New `microgpt chat` subcommand:

```
microgpt chat --model-dir output --temperature 0.5
```

- Uses `rustyline` for line editing and history.
- Maintains a running `Vec<usize>` of conversation tokens.
- Each user input is appended as
  `[user_token, ...encoded_input..., end_turn, assistant_token]`.
- Calls `generate_from_prompt` with `stop_token = end_turn`.
- Streams output character by character via the `on_token` callback.
- Appends assistant response + `end_turn` to history.
- Truncates history from the front when it exceeds `block_size`.
- Supports `/quit` and `/clear`.

### 5. Chat HTTP endpoint (easy, API)

New route: `POST /microgpt/v1/chat`

**Request:**
```json
{
  "messages": [
    {"role": "user", "content": "hello"},
    {"role": "assistant", "content": "hi"},
    {"role": "user", "content": "how are you?"}
  ],
  "temperature": 0.5,
  "seed": 42
}
```

**Response:**
```json
{
  "role": "assistant",
  "content": "..."
}
```

The handler encodes the messages array as a token sequence using the
tokenizer's chat helpers, appends `assistant_token`, calls
`generate_from_prompt`, and decodes the result.

Returns 400 if the model was not trained with chat tokens.

## File changes

| File | Change |
|------|--------|
| `libs/microgpt/src/model.rs` | Add `ModelConfig`, update `Gpt`/`InferenceGpt`, add `generate_from_prompt` |
| `libs/microgpt/src/data.rs` | Add `SpecialTokens`, chat encoding helpers |
| `libs/microgpt/src/train.rs` | Use `model.config.block_size` instead of constant |
| `libs/microgpt/src/lib.rs` | Export new types |
| `apps/microgpt_cli/Cargo.toml` | Add `rustyline` |
| `apps/microgpt_cli/src/main.rs` | Add `Chat` command and REPL |
| `apis/microgpt_serve/src/types.rs` | Add `ChatRequest`, `ChatResponse`, `Message` |
| `apis/microgpt_serve/src/service.rs` | Add `chat_post` handler |
| `apis/microgpt_serve/src/main.rs` | Add `/microgpt/v1/chat` route |

## Not in scope

- BPE / subword tokenization (follow-up).
- Training on conversational data (follow-up).
- SSE streaming for the HTTP endpoint (follow-up).
- System prompts (follow-up).
