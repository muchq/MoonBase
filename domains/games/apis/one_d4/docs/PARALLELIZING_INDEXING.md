# Parallelizing Indexing

Currently indexing is **strictly sequential**: one worker thread, one index request at a time, and within a request **one game at a time** (fetch month → for each game: extract features → insert → next game). That is too slow for bulk or many-player workloads. This document outlines options to parallelize without changing the external API or schema.

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
  - **CPU-bound:** Use **Rayon** to run "replay + detect" per game in parallel (e.g. `games.par_iter().map(|pgn| extract_features(pgn)).collect()`). Each task owns its position state; no shared mutable board.
  - **I/O-bound:** If the source is PGN files or HTTP, **Tokio** (or similar) can overlap many fetches or many game reads with CPU work; a bounded channel can feed PGN strings to a Rayon pool for extraction, then a single writer task for batch DB/Parquet writes.
- pgn-reader's visitor model works in a per-game task: one position per game, replay in that task, run all detectors, return a small result struct; no shared state across games.

### Suggested order of work (Java, before or without a rewrite)

1. **Batch inserts** — Implement `GameFeatureStore.insertBatch` (and batch motif occurrences) and have the worker collect a batch (e.g. 100 games) before writing. Low risk, immediate win.
2. **Parallel games within a month** — Fixed thread pool (e.g. 4–8), submit each game to the pool, collect `GameFeature` + occurrences, then batch insert when a batch is full or the month is done. Keeps a single "logical" worker and request ordering; only the CPU part is parallel.
3. **Multiple worker threads** — If a single request is still slow, run 2–4 index-worker threads so multiple index requests are processed concurrently. Tune pool size and DB/API limits.

### Constraints to keep in mind

- **Chess.com API:** Rate limits and politeness; avoid blasting many requests in parallel from the same key.
- **DB:** Unique constraints and FKs (e.g. game URL, request ID); batch inserts must respect order or use conflict handling if applicable.
- **Memory:** Concurrency = more games in flight. IN_PROCESS_MODE.md notes ~20 MB per concurrent replay; cap parallelism so that total replay state fits in memory.
- **Observability:** With parallelism, log and metrics (e.g. games/sec, queue depth, batch size) help tune and debug.
