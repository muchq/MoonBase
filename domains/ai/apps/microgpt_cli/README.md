# microgpt (CLI)

CLI for training, generation, chat, and model inspection. Wraps the full
microgpt workflow in a single binary with subcommands.

## Subcommands

### `train`

Train a model on a text file (one document per line) or JSONL conversations.

```bash
# Text mode (default)
cargo run -p microgpt_cli -- train --input names.txt --output output --steps 1000

# Chat mode from JSONL conversations
cargo run -p microgpt_cli -- train --input convos.jsonl --output chat-model --chat \
  --n-embd 128 --n-head 8 --n-layer 4 --block-size 256 --steps 10000
```

| Flag | Default | Description |
|------|---------|-------------|
| `--input` | required | Path to training data |
| `--output` | `output` | Directory to save weights and metadata |
| `--steps` | 1000 | Number of training steps |
| `--lr` | 0.01 | Learning rate |
| `--seed` | 42 | Random seed |
| `--chat` | off | Enable chat mode (input must be JSONL) |
| `--n-embd` | 16 | Embedding dimension |
| `--n-head` | 4 | Number of attention heads (must divide n_embd) |
| `--n-layer` | 1 | Number of transformer layers |
| `--block-size` | 16 | Context window size in tokens |

### `generate`

Generate unconditional samples from a trained model.

```bash
cargo run -p microgpt_cli -- generate --model-dir output --num-samples 20 --temperature 0.5
```

### `chat`

Interactive multi-turn chat REPL. Requires a model trained with `--chat`.

```bash
cargo run -p microgpt_cli -- chat --model-dir chat-model --temperature 0.5
```

Type `/quit` to exit, `/clear` to reset conversation history.

### `info`

Print model metadata from a saved checkpoint.

```bash
cargo run -p microgpt_cli -- info --model-dir output
```
