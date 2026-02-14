# microgpt tensor engine

Replace the scalar autograd engine with batched tensor operations to
make training feasible at chat-capable model sizes.

## Problem

The current `Value` autograd engine heap-allocates an `Rc<RefCell<ValueInner>>`
for every scalar operation. A model large enough to learn simple chat
patterns (n_embd=128, n_layer=6, block_size=512) would take ~1 year to
train. The algorithm is correct — the bottleneck is purely per-element
overhead.

## Architecture

The training and inference paths are already cleanly separated:

```
Training:   Value (autograd) → Gpt → train_step() → weights.json
Inference:  weights.json → InferenceGpt (plain f64) → generate/chat
```

The tensor engine replaces only the training side. Everything downstream
of `weights.json` — InferenceGpt, chat REPL, chat API, tokenizer — is
unchanged.

### What to replace

| Current | Replacement |
|---------|-------------|
| `Value` scalar autograd | Hand-derived backward passes per op |
| `Gpt` (training model) | `TensorGpt` with matrix-valued weights |
| `train_step()` + `Adam` | Batched forward/backward + Adam on matrices |

### What stays

- `InferenceGpt`, `generate_from_prompt()`
- `Tokenizer`, `SpecialTokens`, all data loading
- `ModelConfig`, `ModelMeta`
- Chat REPL, chat API
- Weight serialization format (`HashMap<String, Vec<Vec<f64>>>`)

### Interface contract

The tensor engine must produce the same `weights.json` and `meta.json`
format. `InferenceGpt::load_weights_with_config()` loads them unchanged.

## Design

### Tensor type

A simple 2D matrix is sufficient — no need for a general N-dimensional
tensor library. The GPT architecture only uses 1D vectors (activations)
and 2D matrices (weights).

```rust
struct Mat {
    data: Vec<f64>,      // row-major storage
    rows: usize,
    cols: usize,
}
```

### Operations with hand-derived gradients

Since the architecture is fixed, we can write forward + backward for
each operation directly instead of building a general computational
graph:

| Operation | Forward | Backward (dL/dW, dL/dx) |
|-----------|---------|--------------------------|
| `linear(x, W)` → `y = Wx` | matmul | `dW = dy · xᵀ`, `dx = Wᵀ · dy` |
| `rmsnorm(x)` | normalize | chain rule through scale |
| `softmax(x)` | exp/normalize | Jacobian-vector product |
| `relu(x)` | max(0, x) | indicator |
| `cross_entropy(logits, target)` | -log(softmax[target]) | softmax - one_hot |
| `attention(Q, K, V)` | QKᵀ/√d → softmax → ·V | standard attention backward |

Each layer's backward pass chains these together. No graph traversal,
no heap allocation per operation.

### Expected speedup

| Source | Factor |
|--------|--------|
| Eliminate per-scalar heap alloc | ~10-50x |
| Batch operations (loop over matrix, not individual values) | ~10-20x |
| Better cache locality (contiguous memory vs pointer chasing) | ~2-5x |
| **Total (conservative)** | **~200-1000x** |

This puts the "simple memorized responses" model (2M params, 100K steps)
from ~1 year down to **hours to days** on CPU.

### Optional: SIMD / parallelism

Further gains possible but not required for first version:
- Rayon for parallel matmul across rows
- Platform SIMD intrinsics for dot products
- BLAS backend (link to OpenBLAS/Accelerate)

## Validation strategy

Keep the scalar `Value` engine and `Gpt` as a reference implementation.
For a small model config, verify that:

1. Forward pass outputs match between `Gpt` and `TensorGpt`
2. Gradients match after one backward pass
3. Weights match after N training steps with the same seed

This is already easy since `InferenceGpt::forward()` provides a
third independent implementation of the same math using plain f64.

## File plan

| File | Purpose |
|------|---------|
| `libs/microgpt/src/tensor.rs` | `Mat` type, matmul, element-wise ops |
| `libs/microgpt/src/tensor_model.rs` | `TensorGpt`, forward + backward |
| `libs/microgpt/src/tensor_train.rs` | Training loop, Adam on matrices |

The existing `value.rs`, `model.rs` (Gpt portion), and `train.rs` stay
for reference and correctness testing.
