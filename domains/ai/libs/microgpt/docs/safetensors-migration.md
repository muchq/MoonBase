# Safetensors migration

Replace the JSON weight format with safetensors for model weights and optimizer
state. Add an f16 export for inference-only deployment.

## Motivation

The current format serializes all tensors as `HashMap<String, Vec<Vec<f64>>>`
via `serde_json`. For an 8M-param model trained on OASST2:

| File | Current size | safetensors f32 | safetensors f16 |
|------|-------------|-----------------|-----------------|
| `weights.safetensors` | 154 MB (json) | 31 MB | 15 MB |
| `optimizer_m.safetensors` | 154 MB (json) | 31 MB | n/a |
| `optimizer_v.safetensors` | 154 MB (json) | 31 MB | n/a |
| `meta.json` | ~50 KB | ~50 KB | ~50 KB |
| `train_state.json` | <1 KB | <1 KB | <1 KB |
| **Total checkpoint** | **~462 MB** | **~93 MB** | — |
| **Inference model** | **~154 MB** | **~31 MB** | **~15 MB** |

Additional benefits:
- **Zero-copy load**: safetensors is memory-mappable. `InferenceGpt` can back
  its `state_dict` directly from the file without parsing or allocating.
- **Faster checkpoints**: writing raw bytes is significantly faster than JSON
  serialization of millions of floats.
- **No new deps**: `safetensors` is already in `Cargo.lock` via candle.

## File layout

Checkpoint directory (training):
```
chat-model/
  meta.json                  # unchanged — ModelMeta with config + charset
  weights.safetensors        # model weights, f32
  train_state.json           # unchanged — step counters
  optimizer_m.safetensors    # Adam first moments, f32
  optimizer_v.safetensors    # Adam second moments, f32
```

Inference-only directory (exported):
```
chat-model/
  meta.json
  weights.safetensors        # f16 (or f32 if --no-half)
```

## Changes by file

### 1. `microgpt/Cargo.toml` — add safetensors dep

```toml
safetensors = "0.7"
```

### 2. `microgpt/src/tensor_model.rs` — save/load via safetensors

Replace `save_weights() -> String` with `save_weights_safetensors() -> Vec<u8>`:

```rust
use safetensors::tensor::{Dtype, TensorView, serialize};
use safetensors::SafeTensors;

pub fn save_weights_safetensors(&self) -> Vec<u8> {
    let data = self.varmap.data().lock().unwrap();
    let tensors: Vec<(String, TensorView)> = data.iter().map(|(name, var)| {
        let t = var.as_tensor();
        // candle tensors are already contiguous f32
        let bytes: Vec<u8> = t.to_vec1::<f32>().unwrap()
            .iter().flat_map(|v| v.to_le_bytes()).collect();
        let shape: Vec<usize> = t.dims().to_vec();
        (name.clone(), TensorView::new(Dtype::F32, shape, &bytes))
    }).collect();
    serialize(&tensors, &None).unwrap()
}
```

Replace `load_weights_with_config` to deserialize from `&[u8]` instead of
`&str`, using `SafeTensors::deserialize` and building tensors from the raw
byte slices.

Keep the existing JSON methods as `save_weights_json` / `load_weights_json`
behind a `legacy-json` feature flag for one release cycle.

### 3. `microgpt/src/tensor_train.rs` — optimizer state

Replace `save_m() -> String` / `save_v() -> String` with safetensors
equivalents that return `Vec<u8>`. Same pattern as weights — iterate the
`HashMap<String, Tensor>`, serialize each as an f32 safetensors tensor.

Replace `load_state(m_json, v_json, step)` to accept `&[u8]` slices.

### 4. `microgpt/src/model.rs` — InferenceGpt

Add `load_safetensors(vocab_size, bytes: &[u8], config) -> Result<Self>`:

```rust
pub fn load_safetensors(
    vocab_size: usize,
    bytes: &[u8],
    config: ModelConfig,
) -> Result<Self, String> {
    let st = SafeTensors::deserialize(bytes)
        .map_err(|e| format!("failed to load safetensors: {e}"))?;
    let mut state_dict = HashMap::new();
    for (name, tensor) in st.tensors() {
        let data = tensor.data();
        let shape = tensor.shape();
        // Convert raw le bytes to Vec<Vec<f64>>
        let floats: Vec<f64> = data.chunks_exact(4)
            .map(|c| f32::from_le_bytes(c.try_into().unwrap()) as f64)
            .collect();
        let rows = shape[0];
        let cols = shape[1];
        let mat: Vec<Vec<f64>> = (0..rows)
            .map(|r| floats[r*cols..(r+1)*cols].to_vec())
            .collect();
        state_dict.insert(name.to_string(), mat);
    }
    Ok(InferenceGpt { state_dict, vocab_size, config })
}
```

Later optimization: replace `FMatrix = Vec<Vec<f64>>` with a flat `Vec<f32>`
+ stride to enable true zero-copy mmap from safetensors. That's a larger
refactor and can be done separately.

### 5. `microgpt_cli/src/train.rs` — checkpoint I/O

Update `save_checkpoint` and `save_tensor_model`:
- Write `weights.safetensors` instead of `weights.json`
- Write `optimizer_m.safetensors` / `optimizer_v.safetensors`
- `meta.json` and `train_state.json` stay as JSON (small, human-readable)

Update `load_meta_and_weights` to return `Vec<u8>` instead of `String`,
reading from `weights.safetensors`. Fall back to `weights.json` if the
safetensors file doesn't exist (backward compat).

### 6. `microgpt_cli/src/infer.rs` — inference loading

Update `load_inference_model` to use the new `Vec<u8>` path. Update
`run_info` similarly.

### 7. `microgpt_serve/src/main.rs` — serve loading

Same change as infer — read bytes from `weights.safetensors`, call
`InferenceGpt::load_safetensors`.

### 8. `microgpt_cli` — add `export` subcommand

New subcommand to export an inference-only model with optional f16
conversion:

```
microgpt export --model-dir chat-model --output chat-model-f16 --half
```

This reads the checkpoint, optionally converts f32 → f16, and writes
only `meta.json` + `weights.safetensors` (no optimizer state). The `--half`
flag uses `Dtype::F16` in the safetensors output. `InferenceGpt::load_safetensors`
handles both f16 and f32 by checking `tensor.dtype()` and converting
accordingly.

## Backward compatibility

- For one release, `load_meta_and_weights` falls back to `weights.json` if
  `weights.safetensors` is missing. This lets existing checkpoints load
  without manual conversion.
- `microgpt export` can also serve as a migration tool — point it at an old
  JSON checkpoint and it writes safetensors.
- After one release, remove the JSON weight codepath and the `legacy-json`
  feature flag.

## Migration order

1. Add `safetensors` dep to `microgpt/Cargo.toml`
2. Add `save_weights_safetensors` / `load_safetensors` to lib (tensor_model,
   model, tensor_train) alongside existing JSON methods
3. Update CLI train to write safetensors, with fallback read
4. Update CLI infer + serve to read safetensors, with fallback
5. Add `export` subcommand with `--half`
6. Update Homebrew formula to ship f16 model
7. Remove JSON weight methods after one release cycle
