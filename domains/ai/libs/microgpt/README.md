# microgpt

A minimal GPT implementation in pure Rust with a from-scratch autograd engine.
Ported from [karpathy's microgpt.py](https://gist.github.com/aaylward/f9cfa5bff5aada3dcce46db0110eb34e) — the complete algorithm; everything else is just efficiency.

## Architecture (3 crates)

```
domains/ai/libs/microgpt          ← core library (autograd, model, training, data)
domains/ai/apps/microgpt_cli      ← CLI for training, generation, chat, and inspection
domains/ai/apis/microgpt_serve    ← HTTP inference service (server_pal)
```

## Core library modules

| Module | Purpose |
|--------|---------|
| `value.rs` | Scalar autograd engine (`Value` type) with forward ops and reverse-mode backprop via topological sort |
| `model.rs` | GPT model — token/positional embeddings, multi-head self-attention, feed-forward MLP, RMSNorm. Also contains `InferenceGpt` (plain f64, `Send + Sync`) for serving |
| `train.rs` | Training loop — cross-entropy loss, Adam optimizer with learning-rate decay, temperature-controlled sampling |
| `data.rs` | Character-level tokenizer, dataset loader, and chat conversation encoder |

## Key design decisions

- **Autograd via `Rc<RefCell>`**: `Value` uses shared ownership to build a computational graph during the forward pass. `backward()` topologically sorts the graph and propagates gradients in reverse — identical semantics to the Python original.
- **Dual model types**: `Gpt` (with `Value`) carries the autograd graph for training. `InferenceGpt` (plain `f64`) is `Send + Sync` and used by the HTTP server behind `Arc`. Both share the same forward-pass logic.
- **No external ML dependencies**: The only deps are `serde`/`serde_json` for weight serialization. The autograd, optimizer, and model are all self-contained.
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
