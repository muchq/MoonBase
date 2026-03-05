# Chariot (Java) vs Shakmaty (Rust) — Feature Gap for Indexing Rewrite

This document compares **chariot** (`io.github.tors42:chariot`, Java) and **shakmaty** (Rust, with **pgn-reader**) for the one_d4 indexing pipeline: PGN reading, position replay, and motif/feature extraction. It summarizes what the current pipeline uses, what Phase 9 plans to use, and whether shakmaty covers those needs so you can assess the cost of a Rust rewrite.

---

## What one_d4 Uses Today (Chariot)

### 1. PGN movetext → list of SAN moves

- **Chariot:** `PGN.Text.parse(moveText)` returns a stream of tokens; filter `PGN.Text.Move`, get `.san()` per move.
- **Location:** `GameReplayer.extractMoves()`.
- **Note:** NAGs (e.g. `$1`) can leak through as Move tokens with invalid SAN; `isValidSan()` filters them before replay.

### 2. Replay and FEN generation

- **Chariot:** `Board.ofStandard()`, then for each SAN: `board = board.play(san)`, and `board.toFEN()` after each move.
- **Location:** `GameReplayer.replay()`.
- **Output:** `List<PositionContext>` with `(moveNumber, fen, whiteToMove, lastMove)`.

### 3. Board model for detectors (not Chariot)

- Detectors do **not** use Chariot’s `Board` for analysis.
- They take the FEN from `PositionContext`, parse only the **placement** (first segment) via `PinDetector.parsePlacement()` into an `int[][]` (8×8, piece codes ±1..±6).
- All piece logic (attacks, pins, who gives check, etc.) is in **BoardUtils** and detector code: `pieceAttacks()`, `isPathClear()`, `countAttackers()`, `findKing()`, `findCheckingPiece()`, `squareName()`, `pieceNotation()`, `destinationSquare()`, `parsePromotionDestination()`.

So for **current** motif detection, Chariot is used only for: (a) parsing movetext into SANs, and (b) replaying SANs to produce a sequence of FENs.

---

## What Phase 9 Plans to Use (Chariot)

From `ROADMAP.md` and `DATAFUSION_PARQUET.md`:

- **Check attribution:** Distinguish “promotion with check” (promoted piece gives check) vs “discovered check” (another piece gives check). Requires knowing **which piece(s) give check** after each move.
- **Double check:** Two pieces give check at once.
- **New motifs:** back rank mate, smothered mate, zugzwang, double check, overloaded piece — several need full board state and “who gives check” style queries.

So the only **additional** Chariot dependency planned is: **query which piece(s) are giving check** (and possibly “did the checker move this half-move?”). The rest is still FEN/placement + custom logic or Chariot’s board if it exposes it.

---

## Shakmaty + pgn-reader Capabilities

- **Chess vocabulary & position:** `Chess` (default start), `Position` trait, `board()`, `piece_at(Square)`, `king_of(Color)`, FEN parse/write, SAN parse and `San::to_move(&pos)`, `play(m)` / `play_unchecked(m)`.
- **Game state:** `is_check()`, `is_checkmate()`, `is_stalemate()`, `outcome()`, `legal_moves()`, etc.
- **Check attribution:** `checkers()` → **Bitboard of squares of pieces giving check**. With `board().piece_at(sq)` you get exactly “which pieces give check.” So: promoted-piece check vs discovered check vs double check is implementable.
- **Attacks / rays:** `attacks::attacks(piece, square, occupied)`, `bishop_attacks`, `rook_attacks`, `queen_attacks`, `knight_attacks`, `king_attacks`, `between()`, `ray()`, `aligned()`.
- **FEN:** `fen::Fen` parse, `Epd::from_position()`, `Board::from_str()` for placement-only; full position via `Fen::into_position(CastlingMode::Standard)`.
- **Variants:** Optional `variant` feature; Lichess variants supported.
- **pgn-reader:** Streaming, visitor-based PGN reader; `Visitor::san(&mut self, movetext, san_plus)` receives `SanPlus` (SAN + check/checkmate suffix). Integrates with shakmaty: e.g. `san_plus.san.to_move(&pos)` then `pos.play_unchecked(m)` to replay. No move legality in the reader itself (you validate via shakmaty when replaying).

---

## Feature-by-Feature Comparison

| Need | Chariot | Shakmaty + pgn-reader | Gap? |
|------|--------|------------------------|------|
| Parse movetext → SAN list | `PGN.Text.parse()` → Move tokens → `.san()` | pgn-reader `Visitor::san(_, _, san_plus)`; collect SAN or replay in visitor | **No.** Different style (visitor vs stream); same capability. Filter invalid SAN/NAG in visitor or when replaying. |
| Start position | `Board.ofStandard()` | `Chess::default()` / `Chess::new()` | **No.** |
| Play SAN, get next position | `board.play(san)` (returns new Board) | `san.to_move(&pos)?` then `pos.play(m)` or `play_unchecked` | **No.** |
| FEN after each move | `board.toFEN()` | `Fen::from_position()` / `Epd::from_position()` or board FEN from `pos.board()` | **No.** |
| Which pieces give check (Phase 9) | Assumed: query board for checkers | `pos.checkers()` → Bitboard; iterate squares, `pos.board().piece_at(sq)` | **No.** Shakmaty gives checker squares directly. |
| Double check | — | `pos.checkers().count() >= 2` | **No.** |
| Piece at square / king square | Not used in detectors (custom int[][]) | `board.piece_at(sq)`, `board.king_of(color)` | **No.** |
| Sliding/ray attacks for pins, skewers, etc. | Custom in BoardUtils | `attacks::*` with `Bitboard` occupancy | **No.** Can reimplement detector logic with bitboards or same ideas. |
| PGN headers | one_d4 uses custom regex in `PgnParser` | pgn-reader `Visitor::tag()`; no change to “headers separate from movetext” | **No.** |
| Lichess API client | Chariot is a Lichess API client (HTTP, auth, etc.) | N/A (shakmaty is not an API client) | **Only if** the Rust indexer must talk to Lichess API. For indexing from PGN (e.g. dumps or one_d4-provided PGN), **no gap**. |

---

## Summary: Cost of a Rust Rewrite (Library Side)

- **Current pipeline:** Chariot is used only for (1) SAN extraction from movetext and (2) replay → FEN sequence. All motif logic uses custom FEN placement parsing + BoardUtils. **Shakmaty + pgn-reader** can cover (1) and (2) and provide a better foundation for (3) by using `Position` + `checkers()` + `attacks::*` instead of hand-rolled int[][] and helpers.
- **Phase 9 (check attribution, double check, new motifs):** Requires “which pieces give check” and full board state. **Shakmaty** provides `checkers()`, `board()`, `piece_at`, `king_of`, and full attack/ray APIs, so there is **no feature gap** vs the planned Chariot usage.
- **Real cost** of the rewrite is **reimplementing the detector logic** (pin, skewer, fork, attack, check, promotion, back-rank mate, smothered mate, etc.) in Rust using shakmaty’s types (and optionally bitboards), not missing library features. You may also need to replicate the current “last move” / SAN context (e.g. destination square, promotion square) from the replayed move or SAN string, which is straightforward with shakmaty’s `Move` and SAN.

---

## Follow-up: Parallelizing Indexing

Currently indexing is **strictly sequential**: one worker thread, one index request at a time, and within a request **one game at a time** (fetch month → for each game: extract features → insert → next game). That is too slow for bulk or many-player workloads. This section outlines options to parallelize without changing the external API or schema.

### Current bottleneck

- **IndexWorkerLifecycle:** Single thread polls the queue and calls `worker.process(message)`; no overlap between index requests.
- **IndexWorker.process():** For each month, `chessClient.fetchGames(player, month)` (one HTTP call, ~10–100 games), then a **sequential** loop over games: `featureExtractor.extract(game.pgn())` then `gameFeatureStore.insert(row)` + `insertOccurrences()`. So per month, games are processed one-by-one.
- **Costs:** Chess.com API latency (~200 ms/request) dominates when many months are requested; for a single month, PGN replay + motif detection (~2–5K games/sec in the Lichess bulk-ingest estimate) and DB writes are the limit. So both I/O and CPU matter.

### Parallelization options

**1. Parallelize games within a month (Java)**

- After `fetchGames(player, month)`, process games concurrently instead of a single `for (PlayedGame game : ...)` loop.
- Use a fixed-size executor (e.g. `Executors.newFixedThreadPool(N)`) or `parallelStream()` so that up to N games are in extraction at once. N can be sized by CPU cores and memory (each replay is on the order of tens of MB of state; see IN_PROCESS_MODE.md).
- **Writes:** `GameFeatureStore.insert` and `insertOccurrences` must be safe to call from multiple threads (connection pool, no shared mutable state). Alternatively, collect results in memory and have a **single writer thread** (or the main loop) perform batched inserts so DB ordering and connection use are predictable.
- **Progress:** Status updates (`requestStore.updateStatus(..., totalIndexed)`) need to be thread-safe; use atomic counters and/or update from one thread that consumes completed-game results.

**2. Multiple worker threads (multiple index requests)**

- Run several worker threads (or a pool) each running the poll loop and `worker.process(message)`. Different messages = different players/periods, so work is independent.
- **Considerations:** Chess.com rate limits (per API key or IP); DB connection pool size; and not overloading the DB with many concurrent index requests. A small N (e.g. 2–4) is a good start.

**3. Batch DB writes**

- Instead of one `insert` + one `insertOccurrences` per game, accumulate a batch of `GameFeature` rows and their occurrence lists (e.g. 50–200 games), then run a batch insert (JDBC `addBatch` / `executeBatch`, or multi-row INSERT). Reduces round-trips and can improve throughput no matter how games are parallelized.

**4. Parallelize months (optional)**

- Months are independent. You could submit each month to an executor (e.g. `CompletableFuture.supplyAsync(...)` per month) so that multiple months are fetched and processed in parallel. Again, watch Chess.com rate limits and DB connections; this is most useful when a single request spans many months.

**5. Rust rewrite: natural parallelism**

- In Rust, a rewrite with shakmaty + pgn-reader fits parallelism well:
  - **CPU-bound:** Use **Rayon** to run “replay + detect” per game in parallel (e.g. `games.par_iter().map(|pgn| extract_features(pgn)).collect()`). Each task owns its position state; no shared mutable board.
  - **I/O-bound:** If the source is PGN files or HTTP, **Tokio** (or similar) can overlap many fetches or many game reads with CPU work; a bounded channel can feed PGN strings to a Rayon pool for extraction, then a single writer task for batch DB/Parquet writes.
- pgn-reader’s visitor model works in a per-game task: one position per game, replay in that task, run all detectors, return a small result struct; no shared state across games.

### Suggested order of work (Java, before or without a rewrite)

1. **Batch inserts** — Implement `GameFeatureStore.insertBatch` (and batch motif occurrences) and have the worker collect a batch (e.g. 100 games) before writing. Low risk, immediate win.
2. **Parallel games within a month** — Fixed thread pool (e.g. 4–8), submit each game to the pool, collect `GameFeature` + occurrences, then batch insert when a batch is full or the month is done. Keeps a single “logical” worker and request ordering; only the CPU part is parallel.
3. **Multiple worker threads** — If a single request is still slow, run 2–4 index-worker threads so multiple index requests are processed concurrently. Tune pool size and DB/API limits.

### Constraints to keep in mind

- **Chess.com API:** Rate limits and politeness; avoid blasting many requests in parallel from the same key.
- **DB:** Unique constraints and FKs (e.g. game URL, request ID); batch inserts must respect order or use conflict handling if applicable.
- **Memory:** Concurrency = more games in flight. IN_PROCESS_MODE.md notes ~20 MB per concurrent replay; cap parallelism so that total replay state fits in memory.
- **Observability:** With parallelism, log and metrics (e.g. games/sec, queue depth, batch size) help tune and debug.

---

## References

- Chariot: https://github.com/tors42/chariot (Java Lichess API client; one_d4 uses `chariot.chess.Board` and `chariot.model.PGN`).
- Shakmaty: https://docs.rs/shakmaty/latest/shakmaty/ (position, moves, FEN, SAN, UCI, attacks, variants).
- pgn-reader: https://docs.rs/pgn-reader/latest/pgn_reader/ (streaming PGN, visitor, SAN via `SanPlus`; integrates with shakmaty for `to_move` and replay).
