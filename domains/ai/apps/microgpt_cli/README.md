# microgpt (CLI)

CLI for training, generation, chat, and model inspection. Wraps the full
microgpt workflow in a single binary with subcommands.

Training uses candle's tensor engine (`TensorGpt`) with batched forward passes
and autograd. On macOS, GPU acceleration is available via Metal.

## Install

```bash
brew install muchq/muchq/microgpt
```

Or build from source:

```bash
cargo install --path domains/ai/apps/microgpt_cli
```

## Subcommands

### `train`

Train a model on a text file (one document per line) or JSONL conversations.

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
```

| Flag | Default | Description |
|------|---------|-------------|
| `--input` | required | Path to training data |
| `--output` | `output` | Directory to save weights and metadata |
| `--steps` | 1000 | Number of training steps |
| `--lr` | 0.01 | Learning rate (lower for larger models, e.g. 0.001-0.003) |
| `--seed` | 42 | Random seed |
| `--chat` | off | Enable chat mode (input must be JSONL) |
| `--n-embd` | 16 | Embedding dimension |
| `--n-head` | 4 | Number of attention heads (must divide n_embd) |
| `--n-layer` | 1 | Number of transformer layers |
| `--block-size` | 16 | Context window size in tokens |
| `--device` | `cpu` | Device: `cpu` or `metal` (requires Metal-enabled build) |

### `generate`

Generate unconditional samples from a trained model.

```bash
microgpt generate --model-dir names-model --num-samples 20 --temperature 0.5
```

### `chat`

Interactive multi-turn chat REPL. Requires a model trained with `--chat`.

```bash
microgpt chat --model-dir chat-model --temperature 0.5
```

Type `/quit` to exit, `/clear` to reset conversation history.

### `info`

Print model metadata from a saved checkpoint.

```bash
microgpt info --model-dir chat-model
```

## Chat training data format

JSONL file where each line is a conversation — an array of messages with `role` and `content`:

```json
[{"role": "user", "content": "hello"}, {"role": "assistant", "content": "hi there!"}]
[{"role": "user", "content": "what is rust?"}, {"role": "assistant", "content": "a systems programming language"}]
```
