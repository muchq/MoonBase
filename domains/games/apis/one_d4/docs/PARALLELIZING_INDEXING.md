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

1. ~~**Batch inserts**~~ ✓ — `GameFeatureStore.insertBatch` and `insertOccurrencesBatch` implemented; `IndexWorker` collects batches of 100 games before flushing.
2. ~~**Parallel games within a month**~~ ✓ — `IndexWorker` now submits each game's extraction to an injected `ExecutorService` (default 4 threads, env `INDEXER_EXTRACTION_THREADS`) and drains futures into the existing batch buffer; DB writes remain single-threaded and batched.
3. **Multiple worker threads** — If a single request is still slow, run 2–4 index-worker threads so multiple index requests are processed concurrently. Tune pool size and DB/API limits.

**6. Unify indexing and reanalysis paths**

- Currently, `IndexWorker` and `AdminController.reanalyze()` each run motif extraction independently. Indexing does fetch → extract → insert features + occurrences in one pass; reanalysis reads stored PGNs → extract → replace occurrences.
- These could be unified by splitting indexing into two phases: **(a)** fetch from Chess.com and insert `game_features` rows (metadata + PGN only, no motif extraction), then **(b)** run the reanalysis path over the just-inserted games to populate `motif_occurrences`.
- **Benefits:** Single motif-extraction code path eliminates drift between the two flows; reanalysis improvements (e.g. parallelism, new detectors) automatically apply to fresh indexing too.
- **Tradeoff:** Extra DB read (fetching PGNs back out right after inserting them). This is likely negligible compared to Chess.com API latency and PGN replay cost.

### Constraints to keep in mind

- **Chess.com API:** Rate limits and politeness; avoid blasting many requests in parallel from the same key.
- **DB:** Unique constraints and FKs (e.g. game URL, request ID); batch inserts must respect order or use conflict handling if applicable.
- **Memory:** Concurrency = more games in flight. IN_PROCESS_MODE.md notes ~20 MB per concurrent replay; cap parallelism so that total replay state fits in memory.
- **Observability:** With parallelism, log and metrics (e.g. games/sec, queue depth, batch size) help tune and debug.
