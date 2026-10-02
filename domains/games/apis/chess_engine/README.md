# chess_engine

Stockfish behind HTTP, for chess bots at games_hub tables (#1618).

```
POST /chess_engine/v1/bestmove
{"fen": "7k/4P3/6K1/8/8/8/8/8 w - - 0 1", "moves": ["g6f6"], "movetimeMs": 200, "elo": 1500}
→ 200 {"uci": "e7e8q"}
```

- `moves` are UCI moves played from `fen`. `movetimeMs` is 10 to 5000, and defaults to 200.
- `elo` is 1320 to 3190, Stockfish's `UCI_Elo` range. Leaving it out plays at full strength.
- A query that could break out of a UCI line is refused with 400 before any engine sees it. The FEN and every move are matched against strict patterns, so neither can contain a newline.
- A position with no legal move gets 422.
- 503 means no engine answered within the movetime plus two seconds.

`ENGINES` Stockfish processes (default 2) are started up front and serve one move at a time each. Each is pinned to `Threads=1` and `Hash=16` so they fit the half-core / 512M deploy: Stockfish's default Threads (every core) is what made bots peg the CPU and miss their deadline. An engine that fails or misses its deadline is killed, and its slot starts a fresh one on next use. Every ask is logged with fen, movetime, elo, the answer or error, and how long it took.

Stockfish is GPL-3. It runs here as a child process, spoken to over UCI, and is never linked into anything; the HTTP boundary is for isolation, so a hung or crashed engine costs a request, not games_hub. Its corresponding source is the `sf_18` release tag. The image ships the official release binary (`@stockfish`, pinned in `bazel/tools.MODULE.bazel`) at `/stockfish/stockfish`, with its license `Copying.txt` beside it.

Internal only: there is no published port and no caddy route. games_hub calls it across the app network on port 8094.

```bash
bazel test //domains/games/apis/chess_engine/...
bazel build //domains/games/apis/chess_engine:chess_engine_image
```
