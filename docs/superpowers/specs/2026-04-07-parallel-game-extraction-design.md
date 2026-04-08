# Parallel Game Extraction in IndexWorker

**Status:** Design
**Date:** 2026-04-07
**Scope:** Option 1 from `domains/games/apis/one_d4/docs/PARALLELIZING_INDEXING.md` — parallelize per-game feature extraction within a month, keeping DB writes, batching, and ordering unchanged.

## Background

`IndexWorker.process()` currently walks each month's games strictly sequentially: for each `PlayedGame`, it calls `FeatureExtractor.extract(pgn)`, builds a row, and appends to a batch that is flushed at `BATCH_SIZE = 100`. The CPU-bound cost is dominated by `extract()` (PGN replay + motif detectors). DB writes are already batched.

This design parallelizes **only** the `extract()` step across a bounded pool. The DB write path and all observable semantics (batch size, flush boundaries, per-month period upsert, progress updates) remain identical.

## Non-goals

- Parallelizing across months (Option 4 in the parent doc).
- Parallelizing across index requests / worker threads (Option 2).
- Any schema, API, or queue changes.
- Changes to reanalysis / unification (Option 6).

## Design

### Injected executor

`IndexWorker` gains a new constructor parameter:

```java
public IndexWorker(
    ChessClient chessClient,
    FeatureExtractor featureExtractor,
    IndexingRequestStore requestStore,
    GameFeatureStore gameFeatureStore,
    IndexedPeriodStore periodStore,
    ExecutorService extractionExecutor) { ... }
```

The executor is provided by `IndexerModule` as a new `@Context` bean:

```java
@Context
@Bean(preDestroy = "shutdown")
public ExecutorService indexExtractionExecutor() {
  int threads = parseThreads(System.getenv("INDEXER_EXTRACTION_THREADS"), 4);
  ThreadFactory tf = r -> {
    Thread t = new Thread(r);
    t.setName("index-extract-" + COUNTER.incrementAndGet());
    t.setDaemon(true);
    return t;
  };
  return Executors.newFixedThreadPool(threads, tf);
}
```

- **Config:** env var `INDEXER_EXTRACTION_THREADS`, default **4**. Values ≤ 0 or unparseable fall back to the default with a warning log.
- **Lifecycle:** Micronaut's `@Bean(preDestroy = "shutdown")` invokes `ExecutorService.shutdown()` on context close.
- **Thread naming:** `index-extract-N` for log/profile visibility.

### Inner loop rewrite

Current per-month loop (pseudocode):

```
for each game in month:
    try { features = extract(pgn); append to batch; flush if full }
    catch { log and skip }
flush remaining
```

New per-month loop:

```
submit all games to executor, collecting List<Future<ExtractResult>> in source order
for each future in source order:
    try {
        result = future.get()
        append result to batch
        flush if full and update status
    } catch (ExecutionException) { log and skip }
flush remaining
```

Key properties:
- **Bounded in-flight work:** at most `threads` extractions running + one in-progress batch buffered.
- **In-order drain (not a correctness requirement):** futures are drained in submission order purely because it's the simplest possible drain loop. Row order in `insertBatch` is not load-bearing — `game_features` rows are keyed by URL, IDs are DB-assigned, and the occurrences map is keyed by URL too. An `ExecutorCompletionService`-based drain would be valid but more code for no observable win at this scope (we still flush at month-end either way).
- **Failure isolation:** an extraction that throws is logged and skipped; other games still land. Mirrors the current per-game `try/catch`.
- **Batching untouched:** `BATCH_SIZE = 100`, flush-at-boundary, flush-at-month-end, and `requestStore.updateStatus` after each flush all remain.
- **No executor shutdown inside `process()`** — the pool is shared across requests and owned by the DI container.

### `ExtractResult` helper

A small private record inside `IndexWorker`:

```java
private record ExtractResult(GameFeature row, String gameUrl,
                             Map<Motif, List<MotifOccurrence>> occurrences) {}
```

Built inside the submitted task so that `buildGameFeature(...)` also runs on the pool thread (cheap, but keeps the drain loop pure glue).

### Thread-safety audit (implementation step)

`FeatureExtractor.extract()` creates local `positions`, `foundMotifs`, and `allOccurrences` per call, but its injected collaborators — `PgnParser`, `GameReplayer`, and `List<MotifDetector>` — are shared across threads. Before the first green concurrency test, verify each of these is stateless (no mutable instance fields touched inside `extract()` / `replay()` / `detect()`).

`MotifDetector` implementations are required to be stateless by contract. If the audit finds one that isn't, that is a design bug to fix at the source — not something to paper over with `ThreadLocal` or per-task construction. `PgnParser` and `GameReplayer` are held to the same standard.

This audit is a discrete step in the plan and blocks the concurrency test from being marked green.

## Test plan (TDD)

All new tests live in `IndexWorkerTest`, constructing `IndexWorker` directly with a real `ExecutorService` and shutting it down in `@AfterEach`.

1. **Concurrency proof (write first, should fail/deadlock)**
   A `FeatureExtractor` mock where `extract()` decrements a `CountDownLatch(2)` then `await()`s it. Run with a pool size of 2 and ≥ 2 games in the month. With the current sequential loop, the test deadlocks (latch never reaches zero); after the loop rewrite it completes. Use a `junit` timeout of a few seconds so "deadlocked" surfaces as a test failure rather than a hang.

2. **Failure isolation**
   Mock `extract()` to throw on one specific PGN; assert all other games in the month land in `insertBatch` and `updateStatus`'s `totalIndexed` reflects survivors only.

3. **All games land (set, not order)**
   Mock returns distinguishable features per game; assert that the union of `List<GameFeature>` arguments passed to `insertBatch` across all flushes equals the input set. Order is not asserted — the drain is in-order today, but that's an implementation detail, not a contract.

4. **Existing tests stay green**
   Update the existing `IndexWorkerTest` setup to construct the worker with a fixed pool (e.g. size 4). No assertion changes needed — this is the behavior-preservation bar.

5. **Module wiring**
   A small unit test on `IndexerModule.indexExtractionExecutor()` (or the helper `parseThreads`) verifying: default when env var unset, respects a valid value, falls back to default on invalid input. No Micronaut context required — test the factory method directly, or extract `parseThreads` as package-private.

### TDD order

1. Concurrency-proof test → red.
2. Add executor constructor param; plumb through `IndexerModule`; update existing tests to pass the pool → existing tests green, concurrency test still red.
3. Thread-safety audit of `FeatureExtractor` collaborators.
4. Rewrite inner loop (submit + drain) → concurrency test green.
5. Failure-isolation test → red → passes once exception handling in the drain loop is wired.
6. "All games land" test → should be green immediately after step 4.
7. Module wiring test.
8. `./scripts/format-all` + `bazel test //...` before pushing.

## Constraints & risks

- **Memory:** ~20 MB per concurrent replay per `IN_PROCESS_MODE.md`. At default `threads=4` that is ~80 MB of transient replay state, well within headroom. Documenting the cap so ops knows not to set it to 32.
- **Chess.com rate limits:** Unaffected — fetching is still one HTTP call per month.
- **DB writes:** Unaffected — still single-threaded, still batched at 100.
- **Thread safety of `FeatureExtractor` collaborators:** the audit step is blocking. `MotifDetector`, `PgnParser`, and `GameReplayer` are required to be stateless; any violation found during the audit is fixed at the source as part of this change, not worked around.

## Out of scope / follow-ups

- Option 2 (multiple worker threads) remains the natural next step once single-request latency is good.
- Option 6 (unification with reanalysis) is orthogonal and not affected by this change.
- Metrics for games/sec and batch flush rate would help tune `INDEXER_EXTRACTION_THREADS`; not implemented here, noted for later.
