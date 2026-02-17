# microgpt-serve

HTTP inference service for microgpt. Loads a trained checkpoint and serves
generation and chat requests via a JSON API.

Built on [server_pal](../../../platform/libs/server_pal) (Axum + tower-http).

## Running

```bash
MODEL_DIR=output PORT=8080 cargo run -p microgpt_serve
```

Set `MODEL_DIR` to the directory containing `weights.json` and `meta.json`
produced by training. Defaults to `./output`.

## Endpoints

### `POST /microgpt/v1/generate`

Generate unconditional samples from the model.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `num_samples` | int | 1 | Number of samples (max 50) |
| `temperature` | float | 0.5 | Sampling temperature |
| `seed` | int | 42 | RNG seed |

```bash
curl -X POST http://localhost:8080/microgpt/v1/generate \
  -H "Content-Type: application/json" \
  -d '{"num_samples": 5, "temperature": 0.5}'
```

Response: `{ "samples": ["alice", "bob", ...] }`

### `POST /microgpt/v1/chat`

Multi-turn chat completion. Requires a model trained with `--chat`.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `messages` | array | required | `[{"role": "user", "content": "..."}]` |
| `temperature` | float | 0.5 | Sampling temperature |
| `seed` | int | 42 | RNG seed |

```bash
curl -X POST http://localhost:8080/microgpt/v1/chat \
  -H "Content-Type: application/json" \
  -d '{"messages": [{"role": "user", "content": "hello"}]}'
```

Response: `{ "role": "assistant", "content": "...", "tokens_dropped": 0 }`

`tokens_dropped` indicates how many tokens of early conversation history were
truncated to fit within the model's context window. 0 means no truncation.

Returns 400 if the loaded model was not trained with chat tokens.
