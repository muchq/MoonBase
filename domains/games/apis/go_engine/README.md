# go_engine

GNU Go behind HTTP, for go bots at games_hub tables (#1668).

```
POST /go_engine/v1/genmove
{"size": 9, "komi": 6.5, "moves": ["E5", "pass"], "level": 7}
→ 200 {"answer": "C3"}
```

- `moves` are every move so far as GTP vertices (columns A–T skipping I, rows from the bottom; `pass`), Black's first and the colours alternating. The answer is the side to move's: a vertex, `pass` or `resign`.
- `"scoring": true` asks instead for the stones GNU Go counts dead (`final_status_list dead`), answered as their vertices, space separated; none for none.
- `size` is 9, 13 or 19; `komi` a multiple of 0.5 within ±150; `level` GNU Go's 1 to 10.
- A query that could break out of a GTP line, or names a point off the board, is refused with 400 before any engine sees it.
- A game the engine refuses a move of gets 422.
- 503 means no engine answered within 10 seconds.

`ENGINES` GNU Go processes (default 2; the consolidated deploy sets 1) are started up front and serve one query at a time each, each replaying the game from a cleared board. An engine that fails or misses its deadline is killed, and its slot starts a fresh one on next use. Every ask is logged with the board, the move count, the level, the answer or error, and how long it took.

GNU Go is GPL-3. It runs here as a child process, spoken to over GTP, and is never linked into anything. There is no release binary: the image ships one built from the 3.8 release source (`@gnugo`, pinned in `bazel/tools.MODULE.bazel` and built by `bazel/3p/gnugo.BUILD`) at `/gnugo/gnugo`, with its license `COPYING` beside it. The tests run that same binary.

Internal only: there is no published port and no caddy route. games_hub calls it across the app network on port 8095.

```bash
bazel test //domains/games/apis/go_engine/...
bazel build //domains/games/apis/go_engine:go_engine_image
```
