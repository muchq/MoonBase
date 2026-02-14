# microgpt-train

Deployable training binary for microgpt. Configured entirely via environment
variables, suitable for containerized or CI workflows.

## Running

```bash
# Text mode (default)
TRAIN_INPUT=names.txt TRAIN_OUTPUT_DIR=output TRAIN_STEPS=1000 \
  cargo run -p microgpt_train

# Chat mode with custom dimensions
TRAIN_INPUT=convos.jsonl TRAIN_CHAT=1 TRAIN_STEPS=10000 \
  TRAIN_N_EMBD=128 TRAIN_N_HEAD=8 TRAIN_N_LAYER=4 TRAIN_BLOCK_SIZE=256 \
  TRAIN_LR=0.005 cargo run -p microgpt_train
```

## Environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `TRAIN_INPUT` | `input.txt` | Path to training data |
| `TRAIN_OUTPUT_DIR` | `output` | Directory to save weights and metadata |
| `TRAIN_STEPS` | 1000 | Number of training steps |
| `TRAIN_SEED` | 42 | Random seed |
| `TRAIN_LR` | 0.01 | Learning rate |
| `TRAIN_CHAT` | unset | Set to any value to enable chat mode (JSONL input) |
| `TRAIN_N_EMBD` | 16 | Embedding dimension |
| `TRAIN_N_HEAD` | 4 | Number of attention heads (must divide n_embd) |
| `TRAIN_N_LAYER` | 1 | Number of transformer layers |
| `TRAIN_BLOCK_SIZE` | 16 | Context window size in tokens |

## Output

Produces `weights.json` and `meta.json` in the output directory. In text mode,
prints 5 generated samples after training completes.
