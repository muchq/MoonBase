# microgpt

A minimal GPT implementation in Rust, using [candle](https://github.com/huggingface/candle) for tensor operations and autograd.
Ported from [karpathy's microgpt.py](https://gist.github.com/aaylward/f9cfa5bff5aada3dcce46db0110eb34e) — the complete algorithm; everything else is just efficiency.

## Architecture (3 crates)

```
domains/ai/libs/microgpt          ← core library (model, training, data)
domains/ai/apps/microgpt_cli      ← CLI for training, generation, chat, and inspection
domains/ai/apis/microgpt_serve    ← HTTP inference service (server_pal)
```

## Core library modules

| Module | Purpose |
|--------|---------|
| `tensor_model.rs` | `TensorGpt` — batched forward pass using candle tensors and `VarMap` for autograd-tracked weights |
| `tensor_train.rs` | Training loop — cross-entropy loss, Adam optimizer (via `candle_nn::AdamW`), linear learning-rate decay |
| `model.rs` | `InferenceGpt` (plain f64, `Send + Sync`) for autoregressive generation and serving. Also defines `ModelConfig` and `ModelMeta` |
| `data.rs` | Character-level tokenizer, dataset loader, and chat conversation encoder |
| `train.rs` | `TrainConfig` (learning rate, Adam hyperparameters, step count) |

## Key design decisions

- **Candle tensor engine**: Training uses `TensorGpt` backed by [candle](https://github.com/huggingface/candle) for batched tensor operations and reverse-mode autograd via `Var`/`VarMap`. This replaces the original scalar autograd engine.
- **Dual model types**: `TensorGpt` (candle tensors) is used for training with full autograd support. `InferenceGpt` (plain `f64`) is `Send + Sync` and used by the HTTP server behind `Arc`. Both share the same weight format.
- **Metal GPU support**: Training can run on Apple Silicon GPUs via candle's Metal backend. Enable with `--features metal` and `--device metal`.
- **Configurable hyperparameters**: `ModelConfig` allows runtime-configurable `n_embd`, `n_head`, `n_layer`, and `block_size`. Defaults match the original gist's educational scale (16/4/1/16).

## Chat support

Models can be trained in chat mode with special tokens for multi-turn conversations:

- **Special tokens**: `<|user|>`, `<|assistant|>`, `<|end_turn|>` are added to the vocabulary when training with `--chat`
- **Training data**: JSONL format where each line is a conversation (array of `{"role": "user|assistant", "content": "..."}` messages)
- **Inference**: `generate_from_prompt()` prefills the KV cache with conversation history, then decodes until the stop token
- **Endpoints**: Chat REPL in the CLI (`chat` subcommand) and `POST /microgpt/v1/chat` in the HTTP API

## Usage

### Train a model

```bash
# Prepare data: one document per line (e.g., names)
curl -o names.txt https://raw.githubusercontent.com/karpathy/makemore/master/names.txt

# Train for 1000 steps
cargo run -p microgpt_cli -- train --input names.txt --output output --steps 1000

# Train with custom model dimensions and learning rate
cargo run -p microgpt_cli -- train --input names.txt --steps 500 --lr 0.005 \
  --n-embd 32 --n-head 4 --n-layer 2 --block-size 64

# Train on Apple Silicon GPU (requires --features metal)
cargo run -p microgpt_cli --features metal -- train --input names.txt --device metal \
  --output output --steps 1000

# Train a chat model from JSONL conversations
cargo run -p microgpt_cli -- train --input convos.jsonl --output chat-model --chat \
  --n-embd 128 --n-head 8 --n-layer 4 --block-size 256 --steps 10000
```

### Generate samples

```bash
cargo run -p microgpt_cli -- generate --model-dir output --num-samples 20 --temperature 0.5

# Inspect model metadata
cargo run -p microgpt_cli -- info --model-dir output
```

### Interactive chat (CLI)

```bash
cargo run -p microgpt_cli -- chat --model-dir chat-model --temperature 0.5
```

### Serve inference over HTTP

```bash
MODEL_DIR=output PORT=8080 cargo run -p microgpt_serve

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
| `temperature` | float | 0.5 | Sampling temperature |
| `seed` | int | 42 | RNG seed |

Response: `{ "samples": ["alice", "bob", ...] }`

### `POST /microgpt/v1/chat`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `messages` | array | required | `[{"role": "user", "content": "..."}]` |
| `temperature` | float | 0.5 | Sampling temperature |
| `seed` | int | 42 | RNG seed |

Response: `{ "role": "assistant", "content": "..." }`

## Saved model format

Training produces two files:

- `weights.json` — model weights as nested JSON arrays
- `meta.json` — model metadata (vocab size, charset, hyperparameters, special tokens)
