# microgpt (CLI)

CLI for training, generation, chat, and model inspection. Wraps the full
microgpt workflow in a single binary with subcommands.

Training uses candle's tensor engine (`TensorGpt`) with batched forward passes
and autograd. Tokenization uses byte-level BPE via the HuggingFace `tokenizers`
crate. On macOS, GPU acceleration is available via Metal.

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

**Performance Note:** On Apple Silicon (M-series chips), use `--device metal` for significant
speedups (1.5x-2x) and reduced memory usage. This defaults to `bf16` precision, which is
highly optimized for this hardware.

```bash
# Quick test: train a name generator (defaults work well for small datasets)
curl -o names.txt https://raw.githubusercontent.com/karpathy/makemore/master/names.txt
microgpt train --input names.txt --output names-model

# Recommended: train a chat model on OASST2 English (5.3k conversations)
# ~8M params, --skip-long drops conversations that don't fit in the context
# window so the model only trains on complete conversations. ~2h on M4 Pro.
microgpt train --input domains/ai/data/oasst2_chat_en.jsonl --output chat-model --chat \
  --skip-long \
  --n-embd 256 --n-head 8 --n-layer 6 --block-size 1024 \
  --lr 0.001 --batch-size 4 --steps 10000 --device metal \
  --checkpoint-every 1000

# Small chat model: faster iteration, good for experimentation
# ~2M params, trains in ~30 min on Metal
microgpt train --input convos.jsonl --output chat-model-sm --chat --skip-long \
  --n-embd 128 --n-head 4 --n-layer 4 --block-size 512 \
  --lr 0.003 --steps 8000 --device metal

# Large chat model: more capacity, needs more data and steps
# ~25M params, benefits from large datasets and long runs
microgpt train --input convos.jsonl --output chat-model-lg --chat --skip-long \
  --n-embd 384 --n-head 8 --n-layer 8 --block-size 1024 \
  --lr 0.001 --steps 100000 --batch-size 4 --device metal \
  --checkpoint-every 5000

# Resume training from a checkpoint for 50k more steps
# Model architecture, learning rate, batch size, AND dataset info are taken from the checkpoint.
microgpt train --resume chat-model --steps 50000
```

#### Recommended parameters by goal

| Goal | n-embd | n-head | n-layer | block-size | lr | steps | params |
|------|--------|--------|---------|------------|------|-------|--------|
| Quick test / names | 64 | 4 | 2 | 256 | 0.01 | 5000 | ~150K |
| Small chat model | 128 | 4 | 4 | 512 | 0.003 | 8000 | ~2M |
| Medium chat model | 256 | 8 | 6 | 1024 | 0.001 | 10000 | ~8M |
| Large chat model | 384 | 8 | 8 | 1024 | 0.001 | 100000 | ~25M |

General rules of thumb:
- **n-embd** must be divisible by **n-head** (head_dim = n_embd / n_head)
- **lr**: lower for larger models (0.01 for tiny, 0.001 for large)
- **block-size**: measured in BPE tokens (~3-4 characters per token). Set it
  to cover your typical document/conversation length. Use `--skip-long` to
  exclude documents that exceed the context window, or leave it off to
  truncate them (the model will see partial documents).
- **--skip-long**: recommended for chat models. Ensures the model only sees
  complete conversations with proper `<assistant> → content → <end_turn>`
  structure. Without it, long conversations are truncated and the model may
  never see how assistant responses end.
- **batch-size**: larger batches improve GPU utilization but use more memory.
  Use 4 for large block-sizes (1024+), 8-16 for smaller ones. Defaults to 8.
- **warmup-steps**: default 200 is good for most runs. Increase to 500+ for
  very high learning rates or very large models.
- **vocab-size**: default 4096 works well. Increase for very large/diverse
  corpora (8192-16384).

#### Training loss targets

| Loss | Perplexity | Quality |
|------|-----------|---------|
| ~4.0 | ~55 | Basic token frequencies learned, mostly incoherent |
| ~3.0-3.5 | ~20-33 | "Babble" — grammatical fragments, knows chat format |
| ~2.5-3.0 | ~12-20 | Locally coherent sentences, wanders topically |
| < 2.5 | < 12 | Requires more params or simpler data |

Random guessing on vocab_size=4096 would be ln(4096) ≈ 8.3.

#### Learning rate warmup

By default, the learning rate linearly ramps from 0 to peak over the first
200 steps (`--warmup-steps 200`), then linearly decays to 0. This prevents
loss spikes in the first few steps caused by Adam's bias correction amplifying
early updates. Set `--warmup-steps 0` to disable (not recommended).

#### Checking data length

To choose an appropriate block-size, check how long your conversations are
in tokens. As a rough guide, BPE produces ~1 token per 3-4 characters:

```bash
python3 -c "
import json
lengths = []
for line in open('convos.jsonl'):
    msgs = json.loads(line.strip())
    char_len = sum(len(m['content']) for m in msgs)
    lengths.append(char_len // 3)  # rough BPE token estimate
lengths.sort()
p50, p90 = lengths[len(lengths)//2], lengths[int(len(lengths)*0.9)]
print(f'conversations: {len(lengths)}, median: ~{p50} tokens, p90: ~{p90} tokens')
print(f'recommended block-size: {p90} or higher')
"
```

#### Train flags

| Flag | Default | Description |
|------|---------|-------------|
| `--input` | required | Path to training data |
| `--output` | `output` | Directory to save weights and metadata |
| `--steps` | 5000 | Number of training steps |
| `--lr` | 0.01 | Peak learning rate (lower for larger models) |
| `--warmup-steps` | 200 | Steps to linearly ramp LR from 0 to peak |
| `--seed` | 42 | Random seed |
| `--chat` | off | Enable chat mode (input must be JSONL) |
| `--skip-long` | off | Drop documents exceeding block-size instead of truncating |
| `--n-embd` | 64 | Embedding dimension |
| `--n-head` | 4 | Number of attention heads (must divide n_embd) |
| `--n-layer` | 2 | Number of transformer layers |
| `--block-size` | 256 | Context window size in BPE tokens |
| `--batch-size` | 8 | Sequences per gradient update |
| `--vocab-size` | 4096 | BPE vocabulary size |
| `--device` | `cpu` | Device: `cpu` or `metal` |
| `--dtype` | dynamic | Weight dtype: `f32`, `bf16`, or `f16`. Defaults to `bf16` on Metal, `f32` otherwise |
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

Higher temperature (0.7-0.9) gives more creative output; lower (0.3-0.5)
gives more repetitive but coherent output.

### `info`

Print model metadata from a saved checkpoint.

```bash
microgpt info --model-dir chat-model
```

### `export`

Export an inference-only model (no optimizer state), optionally converting to f16.

```bash
# Export as f32 safetensors (same precision as training)
microgpt export --model-dir chat-model --output chat-model-export

# Export as f16 (half the file size, minimal quality loss for inference)
microgpt export --model-dir chat-model --output chat-model-f16 --half
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

#### Data cleanup

During loading, conversations are automatically cleaned for training quality:

- **Trailing user turns trimmed**: Conversations ending with a user message
  (no assistant reply) are trimmed to the last complete assistant turn. A
  trailing user question provides no supervision signal for assistant
  generation and teaches the model to produce empty responses.
- **User-only conversations skipped**: Conversations with no assistant turn
  at all are dropped entirely.

Stats are printed at load time, e.g.:

```
data cleanup: 1766 conversations trimmed (trailing user turn removed), 0 skipped (no assistant turn)
```
