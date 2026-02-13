# AI Domain

Artificial Intelligence and Machine Learning libraries and services.

## Apps

- [**impact-mcp**](apps/impact_mcp): A local-first AI agent that helps engineers amplify their impact, close gaps against a customizable rubric, and communicate contributions clearly — for better project results and career growth.
- [**microgpt-train**](apps/microgpt_train): Deployable training app for the microgpt character-level language model. Reads training data, runs the full training loop, and saves weights.
- [**microgpt (cli)**](apps/microgpt_cli): CLI agent for local training, inference, and model inspection. Wraps the full microgpt workflow in a single binary.

## APIs

- [**microgpt-serve**](apis/microgpt_serve): HTTP inference service for microgpt. Loads a trained checkpoint and serves generation requests via a JSON API, built on server_pal.

## Libraries

- [**Neuro**](libs/neuro): A pure Go implementation of a deep neural network library suitable for training classification models on images, text, and audio data. Features support for various layer types, optimizers, and inference utilities.
- [**microgpt**](libs/microgpt): A minimal GPT implementation in pure Rust with a from-scratch autograd engine. Ported from [karpathy's microgpt.py](https://gist.github.com/aaylward/f9cfa5bff5aada3dcce46db0110eb34e) — the complete algorithm; everything else is just efficiency.

---

## microgpt Design & Usage

### Design

microgpt is a faithful Rust port of a minimal, dependency-free GPT implementation. It prioritizes algorithmic clarity — the entire transformer (embeddings, multi-head attention, MLP, RMSNorm) and its training loop (autograd + Adam) are implemented from scratch with no ML framework.

**Architecture (4 crates):**

```
domains/ai/libs/microgpt          ← core library (autograd, model, training, data)
domains/ai/apps/microgpt_train    ← deployable training binary
domains/ai/apis/microgpt_serve    ← HTTP inference service (server_pal)
domains/ai/apps/microgpt_cli      ← CLI agent for local use
```

**Core library modules:**

| Module | Purpose |
|--------|---------|
| `value.rs` | Scalar autograd engine (`Value` type) with forward ops and reverse-mode backprop via topological sort |
| `model.rs` | GPT model — token/positional embeddings, multi-head self-attention, feed-forward MLP, RMSNorm. Also contains `InferenceGpt` (plain f64, `Send + Sync`) for serving |
| `train.rs` | Training loop — cross-entropy loss, Adam optimizer with learning-rate decay, temperature-controlled sampling |
| `data.rs` | Character-level tokenizer and dataset loader (one document per line) |

**Key design decisions:**

- **Autograd via `Rc<RefCell>`**: `Value` uses shared ownership to build a computational graph during the forward pass. `backward()` topologically sorts the graph and propagates gradients in reverse — identical semantics to the Python original.
- **Dual model types**: `Gpt` (with `Value`) carries the autograd graph for training. `InferenceGpt` (plain `f64`) is `Send + Sync` and used by the HTTP server behind `Arc`. Both share the same forward-pass logic.
- **No external ML dependencies**: The only deps are `serde`/`serde_json` for weight serialization. The autograd, optimizer, and model are all self-contained.
- **Hyperparameters**: `n_embd=16`, `n_head=4`, `n_layer=1`, `block_size=16` — matching the original gist's educational scale.

### Usage

#### 1. Train a model (CLI)

```bash
# Prepare data: one document per line (e.g., names)
curl -o names.txt https://raw.githubusercontent.com/karpathy/makemore/master/names.txt

# Train for 1000 steps, saving to ./output/
cargo run -p microgpt_cli -- train --input names.txt --output output --steps 1000

# Or with custom learning rate and seed:
cargo run -p microgpt_cli -- train --input names.txt --steps 500 --lr 0.005 --seed 123
```

#### 2. Train a model (deployable app)

```bash
# Configure via environment variables
TRAIN_INPUT=names.txt TRAIN_OUTPUT_DIR=output TRAIN_STEPS=1000 TRAIN_SEED=42 \
  cargo run -p microgpt_train
```

#### 3. Generate samples (CLI)

```bash
# Generate 20 samples from a trained model
cargo run -p microgpt_cli -- generate --model-dir output --num-samples 20 --temperature 0.5

# Inspect model metadata
cargo run -p microgpt_cli -- info --model-dir output
```

#### 4. Serve inference over HTTP

```bash
# Start the inference server (loads weights from MODEL_DIR)
MODEL_DIR=output PORT=8080 cargo run -p microgpt_serve

# Generate via the API
curl -X POST http://localhost:8080/microgpt/v1/generate \
  -H "Content-Type: application/json" \
  -d '{"num_samples": 5, "temperature": 0.5, "seed": 42}'
```

**API endpoint:**

`POST /microgpt/v1/generate`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `num_samples` | int | 1 | Number of samples (max 50) |
| `temperature` | float | 0.5 | Sampling temperature |
| `seed` | int | 42 | RNG seed for reproducibility |

Response: `{ "samples": ["alice", "bob", ...] }`

#### 5. Saved model format

Training produces two files:

- `weights.json` — model weights as nested JSON arrays
- `meta.json` — model metadata (vocab size, charset, hyperparameters)
