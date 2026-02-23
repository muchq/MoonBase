# one_d4 API

The `one_d4` API provides endpoints for indexing and querying chess game features.

## Endpoints

### Health Check

Check the status of the service and its dependencies.

```bash
curl -X GET http://localhost:8080/health
```

### Create Indexing Request

Starts a background task to index games for a specific player on a platform within a given time range.

```bash
curl -X POST http://localhost:8080/v1/index \
  -H "Content-Type: application/json" \
  -d '{
    "player": "magnuscarlsen",
    "platform": "CHESS_COM",
    "startMonth": "2023-01",
    "endMonth": "2023-12"
  }'
```

### Get Indexing Status

Retrieves the status of a previously submitted indexing request.

```bash
curl -X GET http://localhost:8080/v1/index/{id}
```

Replace `{id}` with the UUID returned from the `POST /v1/index` request.

### Query Games

Query indexed game features using the ChessQL expression language. ChessQL is an expression-based language, not SQL. Do NOT use `SELECT` or `*`.

**Example: Query by rating**
```bash
curl -X POST http://localhost:8080/v1/query \
  -H "Content-Type: application/json" \
  -d '{
    "query": "white_elo > 2500 OR black_elo > 2500",
    "limit": 10,
    "offset": 0
  }'
```

**Example: Query by motifs**
```bash
curl -X POST http://localhost:8080/v1/query \
  -H "Content-Type: application/json" \
  -d '{
    "query": "motif(fork) AND white_elo >= 2400",
    "limit": 5
  }'
```

**Example: Query by opening (ECO)**
```bash
curl -X POST http://localhost:8080/v1/query \
  -H "Content-Type: application/json" \
  -d '{
    "query": "eco = \"B90\" AND platform IN [\"chess.com\", \"lichess\"]",
    "limit": 20
  }'
```

### Available Fields

**Note:** Query fields must use **snake_case** or **dot.notation**, even though the API response returns fields in **camelCase**.

- `white_elo` (or `white.elo`)
- `black_elo` (or `black.elo`)
- `white_username` (or `white.username`)
- `black_username` (or `black.username`)
- `time_class` (or `time.class`)
- `num_moves` (or `num.moves`)
- `eco`
- `result`
- `platform`
- `game_url` (or `game.url`)
- `played_at` (or `played.at`)

### Available Motifs

- `motif(pin)`
- `motif(cross_pin)`
- `motif(fork)`
- `motif(skewer)`
- `motif(discovered_attack)`
