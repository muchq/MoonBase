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
| `tensor_train.rs` | Training loop — cross-entropy loss, custom Adam optimizer with serializable state (for checkpoint/resume), linear learning-rate decay |
| `model.rs` | `InferenceGpt` (plain f64, `Send + Sync`) for autoregressive generation and serving. Also defines `ModelConfig` and `ModelMeta` |
| `data.rs` | Character-level tokenizer, dataset loader, and chat conversation encoder |
| `train.rs` | `TrainConfig` (learning rate, Adam hyperparameters, step count) and `TrainState` (checkpoint step/optimizer state) |

## Key design decisions

- **Candle tensor engine**: Training uses `TensorGpt` backed by [candle](https://github.com/huggingface/candle) for batched tensor operations and reverse-mode autograd via `Var`/`VarMap`. This replaces the original scalar autograd engine.
- **Dual model types**: `TensorGpt` (candle tensors) is used for training with full autograd support. `InferenceGpt` (plain `f64`) is `Send + Sync` and used by the HTTP server behind `Arc`. Both use safetensors for weight serialization.
- **Safetensors format**: Weights and optimizer state are stored as [safetensors](https://github.com/huggingface/safetensors) (f32). Inference-only models can be exported as f16 via `microgpt export --half`, cutting file size in half.
- **Metal GPU support**: Training can run on Apple Silicon GPUs via candle's Metal backend. Enable with `--device metal`.
- **Configurable hyperparameters**: `ModelConfig` allows runtime-configurable `n_embd`, `n_head`, `n_layer`, and `block_size`. Defaults match the original gist's educational scale (16/4/1/16).

## Usage

### Train a model

```bash
# Quick test: train a tiny name generator
curl -o names.txt https://raw.githubusercontent.com/karpathy/makemore/master/names.txt
microgpt train --input names.txt --output names-model --steps 1000

# Train a chat model on Apple Silicon (M4 Pro / 64GB)
# ~2M params, fits comfortably in memory, trains in minutes
microgpt train --input convos.jsonl --output chat-model --chat \
  --n-embd 128 --n-head 8 --n-layer 4 --block-size 256 \
  --lr 0.003 --steps 10000 --device metal

# Larger chat model for longer training runs
# ~8M params, still manageable on 64GB, benefits from more data/steps
microgpt train --input convos.jsonl --output chat-model-lg --chat \
  --n-embd 256 --n-head 8 --n-layer 8 --block-size 512 \
  --lr 0.001 --steps 50000 --device metal

# Long run with periodic checkpoints (every 10k steps)
microgpt train --input convos.jsonl --output chat-model --chat \
  --n-embd 128 --n-head 8 --n-layer 4 --block-size 1024 \
  --lr 0.003 --steps 100000 --device metal --checkpoint-every 10000

# Resume training from a checkpoint for 50k more steps
microgpt train --input convos.jsonl --output chat-model --chat \
  --resume chat-model --steps 50000
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
| `temperature` | float | 0.5 | Sampling temperature |
| `seed` | int | 42 | RNG seed |

Response: `{ "samples": ["alice", "bob", ...] }`

### `POST /microgpt/v1/chat`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `messages` | array | required | `[{"role": "user", "content": "..."}]` |
| `temperature` | float | 0.5 | Sampling temperature |
| `seed` | int | 42 | RNG seed |

Response: `{ "role": "assistant", "content": "...", "tokens_dropped": 0 }`

`tokens_dropped` indicates how many tokens of early conversation history were
truncated to fit within the model's context window. 0 means no truncation.

## Saved model format

Training produces a checkpoint directory:

- `weights.safetensors` — model weights (f32)
- `meta.json` — model metadata (vocab size, charset, hyperparameters, special tokens)
- `train_state.json` — checkpoint state (training step, optimizer step)
- `optimizer_m.safetensors` — Adam first-moment (momentum) vectors (f32)
- `optimizer_v.safetensors` — Adam second-moment (variance) vectors (f32)

Inference-only exports (via `microgpt export`) contain only `weights.safetensors`
(f32 or f16) and `meta.json`.
