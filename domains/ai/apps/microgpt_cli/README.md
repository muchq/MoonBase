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
# Quick test: train a name generator (defaults work well for small datasets)
curl -o names.txt https://raw.githubusercontent.com/karpathy/makemore/master/names.txt
microgpt train --input names.txt --output names-model

# Train a chat model on Apple Silicon
# block-size is the context window in characters — set it to cover your
# typical conversation length. Use `wc -L convos.jsonl` to check.
# ~2M params, trains in minutes on Metal
microgpt train --input convos.jsonl --output chat-model --chat \
  --n-embd 128 --n-head 8 --n-layer 4 --block-size 1024 \
  --lr 0.003 --steps 50000 --device metal

# Recommended: train on OASST2 (13.8k multilingual conversations)
# ~8M params, block-size 1024 covers p95 conversation length
microgpt train --input domains/ai/data/oasst2_chat.jsonl --output chat-model --chat \
  --n-embd 256 --n-head 8 --n-layer 6 --block-size 1024 \
  --lr 0.0005 --steps 200000 --device metal --checkpoint-every 10000

# Larger chat model for longer conversations / more data
# ~8M params, still manageable on 64GB
microgpt train --input convos.jsonl --output chat-model-lg --chat \
  --n-embd 256 --n-head 8 --n-layer 6 --block-size 2048 \
  --lr 0.001 --steps 100000 --device metal

# Checkpoint every 10k steps for long runs
microgpt train --input convos.jsonl --output chat-model --chat \
  --n-embd 128 --n-head 8 --n-layer 4 --block-size 1024 \
  --lr 0.003 --steps 100000 --device metal --checkpoint-every 10000

# Resume training from a checkpoint for 50k more steps
# Model architecture flags are ignored (taken from checkpoint)
microgpt train --input convos.jsonl --output chat-model --chat \
  --resume chat-model --steps 50000
```

**Choosing block-size:** This is a character-level model, so block-size is
measured in characters, not subword tokens. A 500-word conversation is roughly
2500 characters. Set block-size large enough to capture full conversations —
training truncates anything beyond block-size, so the model never learns
patterns it can't see. Check your data:

```bash
python3 -c "
import json, sys
lengths = []
for line in open('convos.jsonl'):
    msgs = json.loads(line.strip())
    lengths.append(sum(len(m['content']) for m in msgs) + 2*len(msgs) + 2)
lengths.sort()
p50, p90 = lengths[len(lengths)//2], lengths[int(len(lengths)*0.9)]
print(f'conversations: {len(lengths)}, median: {p50} chars, p90: {p90} chars')
print(f'recommended block-size: {p90} or higher')
"
```

| Flag | Default | Description |
|------|---------|-------------|
| `--input` | required | Path to training data |
| `--output` | `output` | Directory to save weights and metadata |
| `--steps` | 5000 | Number of training steps |
| `--lr` | 0.01 | Learning rate (lower for larger models, e.g. 0.001-0.003) |
| `--seed` | 42 | Random seed |
| `--chat` | off | Enable chat mode (input must be JSONL) |
| `--n-embd` | 64 | Embedding dimension |
| `--n-head` | 4 | Number of attention heads (must divide n_embd) |
| `--n-layer` | 2 | Number of transformer layers |
| `--block-size` | 256 | Context window size in characters |
| `--device` | `cpu` | Device: `cpu` or `metal` (requires Metal-enabled build) |
| `--resume` | none | Resume training from a checkpoint directory |
| `--checkpoint-every` | 0 | Save checkpoint every N steps (0 = only at end) |

### `generate`

Generate unconditional samples from a trained model.

```bash
microgpt generate --model-dir names-model --num-samples 20 --temperature 0.5
```

### `chat`

Interactive multi-turn chat REPL. Requires a model trained with `--chat`.

```bash
# Uses ~/.config/microgpt/default-chat-model by default (installed by Homebrew)
microgpt chat

# Or point at any model directory
microgpt chat --model-dir chat-model --temperature 0.5
```

Type `/quit` to exit, `/clear` to reset conversation history.

### `info`

Print model metadata from a saved checkpoint.

```bash
microgpt info --model-dir chat-model
```

## Updating the bundled chat model

The Homebrew formula installs a pre-trained chat model to
`~/.config/microgpt/default-chat-model`. This model is pinned to a specific
release and does **not** update automatically when a new version of the CLI is
installed.

To replace it with a model you've trained yourself:

```bash
microgpt train --input convos.jsonl --output ~/.config/microgpt/default-chat-model --chat ...
```

There is currently no automated training pipeline — model updates are manual.
When a new bundled model is published, the Homebrew formula's `chat-model`
resource will be updated to point to it, but you'll need to run
`brew reinstall microgpt` and re-copy the model to pick it up.

## Chat training data format

JSONL file where each line is a conversation — an array of messages with `role` and `content`:

```json
[{"role": "user", "content": "hello"}, {"role": "assistant", "content": "hi there!"}]
[{"role": "user", "content": "what is rust?"}, {"role": "assistant", "content": "a systems programming language"}]
```
