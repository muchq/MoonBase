# microgpt-serve

HTTP inference service for microgpt. Loads a trained checkpoint and serves
generation and chat requests via a JSON API.

Built on [server_pal](../../../platform/libs/server_pal) (Axum + tower-http).

## Running

```bash
MODEL_DIR=output PORT=8080 cargo run -p microgpt_serve
```

Set `MODEL_DIR` to the directory containing `weights.safetensors` and `meta.json`
produced by training (or `microgpt export`). Defaults to `./output`.

## Endpoints

### `POST /microgpt/v1/generate`

Generate unconditional samples from the model.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `num_samples` | int | 1 | Number of samples (max 50) |
| `temperature` | float | 0.5 | Sampling temperature |
| `seed` | int | 42 | RNG seed |
| `max_tokens` | int | block_size | Max tokens per sample (capped at block_size) |

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
| `max_tokens` | int | block_size - prompt_len | Max tokens to generate (capped at remaining context) |

```bash
curl -X POST http://localhost:8080/microgpt/v1/chat \
  -H "Content-Type: application/json" \
  -d '{"messages": [{"role": "user", "content": "hello"}]}'
```

Response: `{ "role": "assistant", "content": "...", "tokens_dropped": 0 }`

`tokens_dropped` indicates how many tokens of early conversation history were
truncated to fit within the model's context window. 0 means no truncation.

Returns 400 if the loaded model was not trained with chat tokens.

## Response latency and output size

Generation runs synchronously — the full response is buffered before sending.
For the chat endpoint this means time-to-first-byte equals total generation
time. Use `max_tokens` to cap output length and reduce wait times
proportionally (fewer tokens = fewer forward passes).

### Future: streaming responses

Switching to streamed output (SSE or chunked transfer encoding) requires
changes at two layers:

**microgpt library (`generate_from_prompt`):** Currently returns a `Vec<usize>`
after generating all tokens. Needs to yield tokens incrementally — either via a
callback that writes to a `tokio::sync::mpsc` channel, or by returning an
iterator/async stream that produces one token at a time.

**microgpt-serve handler:** Replace the `Json<ChatResponse>` return type with
an `Sse<impl Stream<Item = Event>>` (axum's SSE support) or a streaming
`Body`. Each token would be sent as an SSE event as it's produced, matching
the `text/event-stream` pattern used by OpenAI-compatible APIs.

## Rate limiting

Both endpoints share a per-IP rate limit of **5 requests/second** with a burst
of **10**, provided by `server_pal`'s `tower_governor` integration. Requests
over the limit receive `429 Too Many Requests`.
