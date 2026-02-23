# microgpt

A minimal GPT implementation in Rust, using [candle](https://github.com/huggingface/candle) for tensor operations and autograd.
Ported from [karpathy's microgpt.py](https://gist.github.com/aaylward/f9cfa5bff5aada3dcce46db0110eb34e) — the complete algorithm; everything else is just efficiency.

## Architecture (3 crates)

```
domains/ai/libs/microgpt          ← core library (model, training, data)
domains/ai/apps/microgpt_cli      ← CLI for training, generation, chat, and inspection
domains/ai/apis/microgpt_serve    ← HTTP inference service (server_pal)
```

## Install

```bash
brew install muchq/muchq/microgpt
```

## Core library modules

| Module | Purpose |
|--------|---------|
| `tensor_model.rs` | `TensorGpt` — batched forward pass using candle tensors and `VarMap` for autograd-tracked weights |
| `tensor_train.rs` | Training loop — cross-entropy loss, custom Adam optimizer with serializable state (for checkpoint/resume) |
| `model.rs` | `InferenceGpt` (plain f64, `Send + Sync`) for autoregressive generation and serving. Also defines `ModelConfig` and `ModelMeta` |
| `data.rs` | BPE tokenizer (via HuggingFace `tokenizers` crate), dataset loader with automatic chat data cleanup, and chat conversation encoder |
| `train.rs` | `TrainConfig` (learning rate, warmup, Adam hyperparameters, step count) and `TrainState` (checkpoint step/optimizer state) |

## Key design decisions

- **Candle tensor engine**: Training uses `TensorGpt` backed by [candle](https://github.com/huggingface/candle) for batched tensor operations and reverse-mode autograd via `Var`/`VarMap`. This replaces the original scalar autograd engine.
- **Dual model types**: `TensorGpt` (candle tensors) is used for training with full autograd support. `InferenceGpt` (plain `f64`) is `Send + Sync` and used by the HTTP server behind `Arc`. Both use safetensors for weight serialization.
- **BPE tokenization**: Byte-level BPE via the HuggingFace `tokenizers` crate. The tokenizer is trained on the input corpus at the start of each fresh training run. Vocabulary size is configurable (default 4096). Special tokens (`<bos>`, `<user>`, `<assistant>`, `<end_turn>`) are added for chat mode.
- **Safetensors format**: Weights and optimizer state are stored as [safetensors](https://github.com/huggingface/safetensors). Inference-only models can be exported as f16 via `microgpt export --half`, cutting file size in half.
- **Metal GPU support**: Training can run on Apple Silicon GPUs via candle's Metal backend. Enable with `--device metal`. Critical numerical operations (rmsnorm, log_softmax) are computed in F32 for stability even when training in BF16.
- **Learning rate warmup**: Linear warmup from 0 to peak LR over `warmup_steps` (default 200), followed by linear decay to 0. Prevents early loss spikes from Adam's bias correction.
- **Chat data cleanup**: Conversations are automatically trimmed to end at the last assistant turn. Trailing user messages with no reply are dropped (they provide no supervision for assistant generation and teach the model to produce empty responses). Conversations with no assistant turn are skipped entirely.
- **Data filtering (`--skip-long`)**: Optionally exclude training documents/conversations whose tokenized length exceeds `block_size` instead of truncating them. Recommended for chat training so the model always sees complete conversations with proper turn boundaries.
- **Token suppression at inference**: Special tokens (`<bos>`, `<user>`, `<assistant>`) are masked to negative infinity during generation sampling. A `min_tokens` guard temporarily suppresses stop tokens on the first generated position, preventing undertrained models from immediately emitting `<end_turn>`.
- **Output post-processing**: Leading ASCII punctuation is stripped from generated chat responses to work around artifacts from token suppression in undertrained models.
- **Configurable hyperparameters**: `ModelConfig` allows runtime-configurable `n_embd`, `n_head`, `n_layer`, and `block_size`. Defaults match the original gist's educational scale (16/4/1/16).

## Usage

### Train a model

```bash
# Quick test: train a tiny name generator
curl -o names.txt https://raw.githubusercontent.com/karpathy/makemore/master/names.txt
microgpt train --input names.txt --output names-model --steps 1000

# Train a chat model on Apple Silicon (~8M params, ~2h on M4 Pro)
# --skip-long ensures only complete conversations are used for training
microgpt train --input convos.jsonl --output chat-model --chat --skip-long \
  --n-embd 256 --n-head 8 --n-layer 6 --block-size 1024 \
  --lr 0.001 --batch-size 4 --steps 10000 --device metal

# Larger chat model (~25M params)
microgpt train --input convos.jsonl --output chat-model-lg --chat --skip-long \
  --n-embd 384 --n-head 8 --n-layer 8 --block-size 1024 \
  --lr 0.001 --steps 100000 --batch-size 4 --device metal \
  --checkpoint-every 5000

# Resume training from a checkpoint for 50k more steps
microgpt train --resume chat-model --steps 50000
```

### Generate samples

```bash
microgpt generate --model-dir names-model --num-samples 20 --temperature 0.5

# Inspect model metadata
microgpt info --model-dir names-model
```

### Interactive chat

```bash
microgpt chat --model-dir chat-model --temperature 0.5
```

### Chat training data format

JSONL file where each line is a conversation — an array of messages with `role` and `content`:

```json
[{"role": "user", "content": "hello"}, {"role": "assistant", "content": "hi there!"}]
[{"role": "user", "content": "what is rust?"}, {"role": "assistant", "content": "a systems programming language"}]
```

### Serve inference over HTTP

```bash
MODEL_DIR=chat-model PORT=8080 cargo run -p microgpt_serve

# Generate
curl -X POST http://localhost:8080/microgpt/v1/generate \
  -H "Content-Type: application/json" \
  -d '{"num_samples": 5, "temperature": 0.5, "seed": 42}'

# Chat
curl -X POST http://localhost:8080/microgpt/v1/chat \
  -H "Content-Type: application/json" \
  -d '{"messages": [{"role": "user", "content": "hello"}], "temperature": 0.5}'
```

## API endpoints

### `POST /microgpt/v1/generate`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `num_samples` | int | 1 | Number of samples (max 50) |
| `temperature` | float | 0.5 | Sampling temperature (must be >= 0) |
| `seed` | int | 42 | RNG seed |
| `max_tokens` | int | block_size | Max tokens per sample (capped at block_size) |

Response: `{ "samples": ["alice", "bob", ...] }`

Returns 400 if validation fails (e.g. negative temperature, zero num_samples).

### `POST /microgpt/v1/chat`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `messages` | array | required | `[{"role": "user", "content": "..."}]` |
| `temperature` | float | 0.5 | Sampling temperature (must be >= 0) |
| `seed` | int | 42 | RNG seed |
| `max_tokens` | int | block_size - prompt_len | Max tokens to generate (capped at remaining context) |

Response: `{ "role": "assistant", "content": "...", "tokens_dropped": 0 }`

`tokens_dropped` indicates how many tokens of early conversation history were
truncated to fit within the model's context window. 0 means no truncation.

Returns 400 if validation fails (empty messages, unknown role, empty content, negative temperature).
Returns 400 if the loaded model was not trained with chat tokens.

## Saved model format

Training produces a checkpoint directory:

- `weights.safetensors` — model weights (native dtype: f32, bf16, or f16)
- `tokenizer.json` — BPE tokenizer (HuggingFace format)
- `meta.json` — model metadata (vocab size, hyperparameters, special token names)
- `train_state.json` — checkpoint state (training step, optimizer step, learning rate, batch size, dtype)
- `optimizer_m.safetensors` — Adam first-moment (momentum) vectors
- `optimizer_v.safetensors` — Adam second-moment (variance) vectors

Inference-only exports (via `microgpt export`) contain only `weights.safetensors`
(f32 or f16), `tokenizer.json`, and `meta.json`.
