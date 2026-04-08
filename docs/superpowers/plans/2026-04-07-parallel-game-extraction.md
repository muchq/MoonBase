# Parallel Game Extraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Parallelize per-game `FeatureExtractor.extract()` calls within a month inside `IndexWorker`, behind an injected `ExecutorService`, while preserving DB write batching, status updates, and failure-isolation semantics.

**Architecture:** `IndexWorker` gains a constructor-injected `ExecutorService`. Inside the per-month loop, each surviving game is submitted as a task that runs `featureExtractor.extract` and builds the `GameFeature` row. Futures are drained in submission order into the existing batch buffer, which is flushed at `BATCH_SIZE` and at month-end exactly as today. The pool is wired in `IndexerModule` as a fixed-size pool (default 4, env override `INDEXER_EXTRACTION_THREADS`) with Micronaut-managed shutdown.

**Tech Stack:** Java 21, Micronaut DI (`@Factory` / `@Context`), JUnit 4, AssertJ, Bazel build, Failsafe (already used).

**Spec:** `docs/superpowers/specs/2026-04-07-parallel-game-extraction-design.md`

---

## File Structure

**Modified:**
- `domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/worker/IndexWorker.java` — new `ExecutorService` constructor param; rewritten per-month inner loop; new private `ExtractResult` record.
- `domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/IndexerModule.java` — new `@Context` factory method `indexExtractionExecutor()`; new package-private static helper `parseThreads`; updated `indexWorker(...)` factory signature.
- `domains/games/apis/one_d4/src/test/java/com/muchq/games/one_d4/worker/IndexWorkerTest.java` — new helper `newPool()` + `@After tearDown()`; existing setup constructs worker with the pool; three new tests.

**Created:**
- `domains/games/apis/one_d4/src/test/java/com/muchq/games/one_d4/IndexerModuleTest.java` — unit test for `parseThreads`.

**Audited (no expected code change):**
- All `MotifDetector` implementations under `.../one_d4/motifs/`
- `FeatureExtractor`, `PgnParser`, `GameReplayer` under `.../one_d4/engine/`

---

## Task 1: Statelessness audit

`MotifDetector`, `PgnParser`, and `GameReplayer` are required by contract to be stateless. This task verifies that and is the gate for the concurrency test.

**Files:** read-only audit of:
- `domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/motifs/*.java`
- `domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/engine/{FeatureExtractor,PgnParser,GameReplayer}.java`

- [ ] **Step 1: Grep for non-static instance fields in detectors**

Run (via Grep tool):
- pattern: `^\s*private (?!static)`
- path: `domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/motifs`
- output_mode: `content`

Expected: no matches (or only `final` collaborator references that are themselves stateless).

- [ ] **Step 2: Grep for non-static instance fields in engine helpers**

Run (via Grep tool):
- pattern: `^\s*private (?!static)`
- path: `domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/engine`
- output_mode: `content`

Expected: only `private final` references in `FeatureExtractor` (`pgnParser`, `replayer`, `detectors`) — all of which are themselves stateless.

- [ ] **Step 3: Spot-read each detector's `detect(...)` method**

Read each of: `PinDetector`, `CrossPinDetector`, `SkewerDetector`, `AttackDetector`, `CheckDetector`, `BackRankMateDetector`, `SmotheredMateDetector`, `PromotionDetector`, `PromotionWithCheckDetector`, `PromotionWithCheckmateDetector`, `DiscoveredAttackDetector`.

Verify: `detect(List<PositionContext>)` takes input, allocates only locals, returns a fresh list. No writes to instance fields.

- [ ] **Step 4: Spot-read `GameReplayer.replay()` and `PgnParser.parse()`**

Verify: each call allocates fresh state. No mutable instance fields touched.

- [ ] **Step 5: Decision point**

If all classes are stateless: proceed to Task 2. No commit needed (no code changed).

If any class is stateful: **stop** and surface the finding to the user. Per the spec, the fix is to make the class stateless at the source — that becomes a prerequisite task before the rest of the plan continues.

---

## Task 2: Inject `ExecutorService` into `IndexWorker` (existing tests stay green)

This task adds the new constructor parameter and threads it through `IndexerModule` and existing tests. No behavior change yet — the inner loop still runs sequentially. Existing tests must remain green at the end.

**Files:**
- Modify: `domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/worker/IndexWorker.java`
- Modify: `domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/IndexerModule.java`
- Modify: `domains/games/apis/one_d4/src/test/java/com/muchq/games/one_d4/worker/IndexWorkerTest.java`

- [ ] **Step 1: Add `ExecutorService` constructor parameter to `IndexWorker`**

Edit `IndexWorker.java`. Add the import:

```java
import java.util.concurrent.ExecutorService;
```

Add a field:

```java
private final ExecutorService extractionExecutor;
```

Update the constructor signature and body:

```java
public IndexWorker(
    ChessClient chessClient,
    FeatureExtractor featureExtractor,
    IndexingRequestStore requestStore,
    GameFeatureStore gameFeatureStore,
    IndexedPeriodStore periodStore,
    ExecutorService extractionExecutor) {
  this.chessClient = chessClient;
  this.featureExtractor = featureExtractor;
  this.requestStore = requestStore;
  this.gameFeatureStore = gameFeatureStore;
  this.periodStore = periodStore;
  this.extractionExecutor = extractionExecutor;
}
```

Do not change `process()` yet. The field is unused for now — that's intentional.

- [ ] **Step 2: Wire executor through `IndexerModule`**

Edit `IndexerModule.java`. Add imports:

```java
import io.micronaut.context.annotation.Bean;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.atomic.AtomicInteger;
```

Add a static counter and a new `@Context` factory method **above** `indexWorker(...)`:

```java
private static final AtomicInteger EXTRACT_THREAD_COUNTER = new AtomicInteger();

static int parseThreads(String raw, int defaultValue) {
  if (raw == null || raw.isBlank()) {
    return defaultValue;
  }
  try {
    int parsed = Integer.parseInt(raw.strip());
    if (parsed <= 0) {
      LOG.warn("Invalid INDEXER_EXTRACTION_THREADS={}; falling back to {}", raw, defaultValue);
      return defaultValue;
    }
    return parsed;
  } catch (NumberFormatException e) {
    LOG.warn("Unparseable INDEXER_EXTRACTION_THREADS={}; falling back to {}", raw, defaultValue);
    return defaultValue;
  }
}

@Context
@Bean(preDestroy = "shutdown")
public ExecutorService indexExtractionExecutor() {
  int threads = parseThreads(System.getenv("INDEXER_EXTRACTION_THREADS"), 4);
  ThreadFactory tf =
      r -> {
        Thread t = new Thread(r);
        t.setName("index-extract-" + EXTRACT_THREAD_COUNTER.incrementAndGet());
        t.setDaemon(true);
        return t;
      };
  LOG.info("Index extraction executor: fixed pool of {} threads", threads);
  return Executors.newFixedThreadPool(threads, tf);
}
```

Update the existing `indexWorker(...)` factory method to take and forward the executor:

```java
@Context
public IndexWorker indexWorker(
    ChessClient chessClient,
    FeatureExtractor featureExtractor,
    IndexingRequestStore requestStore,
    GameFeatureStore gameFeatureStore,
    IndexedPeriodStore periodStore,
    ExecutorService indexExtractionExecutor) {
  return new IndexWorker(
      chessClient,
      featureExtractor,
      requestStore,
      gameFeatureStore,
      periodStore,
      indexExtractionExecutor);
}
```

- [ ] **Step 3: Update `IndexWorkerTest` setup to construct worker with a real pool**

Edit `IndexWorkerTest.java`. Add imports:

```java
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import org.junit.After;
```

Add a field next to the others:

```java
private ExecutorService extractionExecutor;
```

Update `setUp()` to allocate the pool and pass it through:

```java
@Before
public void setUp() {
  stubChessClient = new StubChessClient();
  requestStore = new RecordingRequestStore();
  periodStore = new StubPeriodStore();
  extractionExecutor = Executors.newFixedThreadPool(4);
  List<MotifDetector> detectors =
      List.of(
          new PinDetector(), new CrossPinDetector(), new SkewerDetector(), new AttackDetector());
  featureExtractor = new FeatureExtractor(new PgnParser(), new GameReplayer(), detectors);
  worker =
      new IndexWorker(
          stubChessClient,
          featureExtractor,
          requestStore,
          new NoOpGameFeatureStore(),
          periodStore,
          extractionExecutor);
}

@After
public void tearDown() {
  extractionExecutor.shutdownNow();
}
```

Then update **every** other place in `IndexWorkerTest.java` that constructs an `IndexWorker` to pass `extractionExecutor` as the sixth argument. Specifically:
- `process_whenGameHasMotifs_callsInsertOccurrencesWithOccurrences` — `workerWithRecording`
- `process_bulletGamesNotSkippedWhenExcludeBulletFalse` — `w`
- `process_bulletGamesSkippedWhenExcludeBulletTrue` — `w`

Each becomes:

```java
new IndexWorker(
    stubChessClient,
    featureExtractor /* or the local one */,
    requestStore,
    recordingStore,
    periodStore,
    extractionExecutor)
```

- [ ] **Step 4: Build and run existing tests**

Run:
```bash
bazel test //domains/games/apis/one_d4:IndexWorkerTest
```

Expected: PASS. All existing tests still green; the field on `IndexWorker` is unused but compiles cleanly.

- [ ] **Step 5: Commit**

```bash
git add domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/worker/IndexWorker.java \
        domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/IndexerModule.java \
        domains/games/apis/one_d4/src/test/java/com/muchq/games/one_d4/worker/IndexWorkerTest.java
git commit -m "refactor(one_d4): inject ExecutorService into IndexWorker (unused, plumbing)"
```

---

## Task 3: Concurrency-proof test (red — proves the loop is sequential today)

Add a test that proves real parallelism. With the current sequential loop it deadlocks; the JUnit timeout converts the deadlock into a clean test failure.

**Files:**
- Modify: `domains/games/apis/one_d4/src/test/java/com/muchq/games/one_d4/worker/IndexWorkerTest.java`

- [ ] **Step 1: Write the failing test**

Add to `IndexWorkerTest.java` (anywhere among the `@Test` methods):

```java
@Test(timeout = 5000)
public void process_runsExtractionsConcurrently_acrossPoolThreads() throws Exception {
  // Two games in one month. Each extract() decrements a 2-latch then awaits it.
  // If extract() runs sequentially, the second call never starts and the latch
  // never reaches zero -> the test times out. With a pool size >= 2, both
  // games are in flight at once and the latch releases immediately.
  java.util.concurrent.CountDownLatch latch = new java.util.concurrent.CountDownLatch(2);
  FeatureExtractor latchExtractor =
      new FeatureExtractor(new PgnParser(), new GameReplayer(), List.of()) {
        @Override
        public GameFeatures extract(String pgn) {
          latch.countDown();
          try {
            if (!latch.await(3, java.util.concurrent.TimeUnit.SECONDS)) {
              throw new RuntimeException("latch never released — extraction is sequential");
            }
          } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new RuntimeException(e);
          }
          return new GameFeatures(
              java.util.EnumSet.noneOf(Motif.class), 0, java.util.Map.of());
        }
      };

  ExecutorService pool = Executors.newFixedThreadPool(2);
  try {
    RecordingGameFeatureStore store = new RecordingGameFeatureStore();
    IndexWorker concurrentWorker =
        new IndexWorker(
            stubChessClient, latchExtractor, requestStore, store, periodStore, pool);
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(
            playedGame("https://chess.com/g/1", MINIMAL_PGN, "blitz"),
            playedGame("https://chess.com/g/2", MINIMAL_PGN, "blitz")));

    concurrentWorker.process(
        new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false));

    assertThat(store.getInsertCount()).isEqualTo(2);
    assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
  } finally {
    pool.shutdownNow();
  }
}
```

Note: this test subclasses `FeatureExtractor` to override `extract`. `FeatureExtractor.extract` is currently non-final — confirm that's the case before relying on the override. If `extract` is `final`, change it to non-final in `FeatureExtractor.java` as part of this step (single-line change), since making test seams overridable is preferable to introducing a Mockito dependency in this test file.

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
bazel test //domains/games/apis/one_d4:IndexWorkerTest --test_filter=process_runsExtractionsConcurrently_acrossPoolThreads
```

Expected: FAIL with timeout (`test timed out after 5000 milliseconds`) — proving the inner loop is currently sequential.

- [ ] **Step 3: Commit the failing test**

```bash
git add domains/games/apis/one_d4/src/test/java/com/muchq/games/one_d4/worker/IndexWorkerTest.java
git commit -m "test(one_d4): add failing concurrency test for IndexWorker extraction"
```

(Optional but recommended: lets the next commit show the fix in isolation.)

---

## Task 4: Rewrite the inner loop to submit + drain (concurrency test goes green)

**Files:**
- Modify: `domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/worker/IndexWorker.java`

- [ ] **Step 1: Add the `ExtractResult` private record and rewrite the per-month inner loop**

In `IndexWorker.java`, add imports:

```java
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Future;
```

Add a private nested record near the bottom of the class:

```java
private record ExtractResult(
    GameFeature row,
    String gameUrl,
    Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences) {}
```

Replace the inner per-game loop in `process()` (currently `IndexWorker.java:106-131`) with:

```java
List<GameFeature> featureBatch = new ArrayList<>();
Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesBatch =
    new LinkedHashMap<>();

// Submit each surviving game to the extraction pool, preserving source order.
List<Future<ExtractResult>> futures = new ArrayList<>();
for (PlayedGame game : response.get().games()) {
  if (message.excludeBullet() && "bullet".equals(game.timeClass())) {
    continue;
  }
  futures.add(
      extractionExecutor.submit(
          () -> {
            GameFeatures features = featureExtractor.extract(game.pgn());
            GameFeature row = buildGameFeature(message, game, features);
            return new ExtractResult(row, game.url(), features.occurrences());
          }));
}

int monthCount = 0;
for (Future<ExtractResult> future : futures) {
  ExtractResult result;
  try {
    result = future.get();
  } catch (ExecutionException e) {
    LOG.warn("Failed to index game", e.getCause());
    continue;
  } catch (InterruptedException e) {
    Thread.currentThread().interrupt();
    LOG.warn("Interrupted while draining extraction futures", e);
    break;
  }
  featureBatch.add(result.row());
  if (!result.occurrences().isEmpty()) {
    occurrencesBatch.put(result.gameUrl(), result.occurrences());
  }
  monthCount++;
  totalIndexed++;
  if (featureBatch.size() >= BATCH_SIZE) {
    flushBatch(featureBatch, occurrencesBatch);
    requestStore.updateStatus(message.requestId(), "PROCESSING", null, totalIndexed);
  }
}
flushBatch(featureBatch, occurrencesBatch);
```

Leave the period upsert and final `updateStatus` calls below this block exactly as they are. The `int finalMonthCount = monthCount;` capture for the lambda still works.

- [ ] **Step 2: Run the concurrency test to verify it passes**

Run:
```bash
bazel test //domains/games/apis/one_d4:IndexWorkerTest --test_filter=process_runsExtractionsConcurrently_acrossPoolThreads
```

Expected: PASS within 5 seconds.

- [ ] **Step 3: Run the full IndexWorkerTest to verify nothing regressed**

Run:
```bash
bazel test //domains/games/apis/one_d4:IndexWorkerTest
```

Expected: all tests PASS.

- [ ] **Step 4: Commit**

```bash
git add domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/worker/IndexWorker.java
git commit -m "feat(one_d4): parallelize per-month feature extraction in IndexWorker"
```

---

## Task 5: Failure-isolation test

Verify that an extraction throwing for one game does not break the rest of the month.

**Files:**
- Modify: `domains/games/apis/one_d4/src/test/java/com/muchq/games/one_d4/worker/IndexWorkerTest.java`

- [ ] **Step 1: Write the failing test**

Add to `IndexWorkerTest.java`:

```java
@Test
public void process_oneFailingExtraction_doesNotPreventOthers() {
  String poisonUrl = "https://chess.com/g/poison";
  FeatureExtractor selectivelyFailing =
      new FeatureExtractor(new PgnParser(), new GameReplayer(), List.of()) {
        @Override
        public GameFeatures extract(String pgn) {
          if (pgn.contains("POISON")) {
            throw new RuntimeException("boom");
          }
          return new GameFeatures(
              java.util.EnumSet.noneOf(Motif.class), 0, java.util.Map.of());
        }
      };

  String poisonPgn =
      """
      [Event "POISON"]
      [Site "Chess.com"]
      [White "W"]
      [Black "B"]
      [Result "1-0"]
      [ECO "C20"]

      1. e4 e5 1-0
      """;

  RecordingGameFeatureStore store = new RecordingGameFeatureStore();
  IndexWorker w =
      new IndexWorker(
          stubChessClient, selectivelyFailing, requestStore, store, periodStore, extractionExecutor);
  stubChessClient.setResponse(
      java.time.YearMonth.of(2024, 1),
      List.of(
          playedGame("https://chess.com/g/ok1", MINIMAL_PGN, "blitz"),
          playedGame(poisonUrl, poisonPgn, "blitz"),
          playedGame("https://chess.com/g/ok2", MINIMAL_PGN, "blitz")));

  w.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false));

  assertThat(store.getInsertCount()).isEqualTo(2);
  assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
  assertThat(requestStore.getLastGamesIndexed()).isEqualTo(2);
}
```

- [ ] **Step 2: Run it to verify it passes**

Run:
```bash
bazel test //domains/games/apis/one_d4:IndexWorkerTest --test_filter=process_oneFailingExtraction_doesNotPreventOthers
```

Expected: PASS — the drain loop's `ExecutionException` catch already handles this. If it fails, inspect the unwrapping: `future.get()` wraps the cause in `ExecutionException`, which the loop logs and skips.

- [ ] **Step 3: Commit**

```bash
git add domains/games/apis/one_d4/src/test/java/com/muchq/games/one_d4/worker/IndexWorkerTest.java
git commit -m "test(one_d4): add failure isolation test for parallel extraction"
```

---

## Task 6: All-games-land test (set equality, not order)

Asserts that every input game appears in `insertBatch` calls — without asserting order, leaving room for a future switch to completion-order draining.

**Files:**
- Modify: `domains/games/apis/one_d4/src/test/java/com/muchq/games/one_d4/worker/IndexWorkerTest.java`

- [ ] **Step 1: Extend `RecordingGameFeatureStore` to capture inserted URLs**

In `IndexWorkerTest.java`, edit the `RecordingGameFeatureStore` inner class. Add a field and capture URLs in `insertBatch`:

```java
private static final class RecordingGameFeatureStore extends NoOpGameFeatureStore {
  private final Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>>
      allInsertedOccurrences = new HashMap<>();
  private final List<String> insertedUrls =
      java.util.Collections.synchronizedList(new ArrayList<>());
  private int insertCount = 0;

  Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> getAllInsertedOccurrences() {
    return allInsertedOccurrences;
  }

  int getInsertCount() {
    return insertCount;
  }

  List<String> getInsertedUrls() {
    return new ArrayList<>(insertedUrls);
  }

  @Override
  public void insertBatch(List<GameFeature> features) {
    insertCount += features.size();
    for (GameFeature f : features) {
      insertedUrls.add(f.gameUrl());
    }
  }

  @Override
  public void insertOccurrencesBatch(
      Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame) {
    allInsertedOccurrences.putAll(occurrencesByGame);
  }
}
```

Note: `GameFeature.gameUrl()` is the accessor — verified against `GameFeature.java` (record component named `gameUrl`).

- [ ] **Step 2: Add the test**

```java
@Test
public void process_allGamesLandInBatch_regardlessOfOrder() {
  List<String> urls =
      List.of(
          "https://chess.com/g/a",
          "https://chess.com/g/b",
          "https://chess.com/g/c",
          "https://chess.com/g/d");
  List<PlayedGame> games = new ArrayList<>();
  for (String u : urls) {
    games.add(playedGame(u, MINIMAL_PGN, "blitz"));
  }
  stubChessClient.setResponse(java.time.YearMonth.of(2024, 1), games);

  RecordingGameFeatureStore store = new RecordingGameFeatureStore();
  IndexWorker w =
      new IndexWorker(
          stubChessClient, featureExtractor, requestStore, store, periodStore, extractionExecutor);

  w.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false));

  assertThat(store.getInsertedUrls()).containsExactlyInAnyOrderElementsOf(urls);
  assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
}
```

- [ ] **Step 3: Run it to verify it passes**

Run:
```bash
bazel test //domains/games/apis/one_d4:IndexWorkerTest --test_filter=process_allGamesLandInBatch_regardlessOfOrder
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add domains/games/apis/one_d4/src/test/java/com/muchq/games/one_d4/worker/IndexWorkerTest.java
git commit -m "test(one_d4): assert all games land via insertBatch (set equality)"
```

---

## Task 7: `IndexerModule.parseThreads` unit test

Cover the env-var parsing helper directly. No Micronaut context needed.

**Files:**
- Create: `domains/games/apis/one_d4/src/test/java/com/muchq/games/one_d4/IndexerModuleTest.java`
- Modify: `domains/games/apis/one_d4/BUILD.bazel` (only if a new test target is needed; otherwise add the source to an existing test target)

- [ ] **Step 1: Inspect the existing test BUILD wiring**

Read `domains/games/apis/one_d4/BUILD.bazel` and locate the `java_test` (or `java_library` test target) that compiles `IndexWorkerTest.java`. Determine whether `IndexerModuleTest.java` can be added to that target's `srcs` (preferred — no new target) or whether a sibling target is the right home.

Adjust the file path of the new test to match the package the existing test target expects (likely `com/muchq/games/one_d4/IndexerModuleTest.java` directly under `src/test/java/`).

- [ ] **Step 2: Write the test**

Create `domains/games/apis/one_d4/src/test/java/com/muchq/games/one_d4/IndexerModuleTest.java`:

```java
package com.muchq.games.one_d4;

import static org.assertj.core.api.Assertions.assertThat;

import org.junit.Test;

public class IndexerModuleTest {

  @Test
  public void parseThreads_returnsDefault_whenNull() {
    assertThat(IndexerModule.parseThreads(null, 4)).isEqualTo(4);
  }

  @Test
  public void parseThreads_returnsDefault_whenBlank() {
    assertThat(IndexerModule.parseThreads("   ", 4)).isEqualTo(4);
  }

  @Test
  public void parseThreads_returnsDefault_whenUnparseable() {
    assertThat(IndexerModule.parseThreads("abc", 4)).isEqualTo(4);
  }

  @Test
  public void parseThreads_returnsDefault_whenNonPositive() {
    assertThat(IndexerModule.parseThreads("0", 4)).isEqualTo(4);
    assertThat(IndexerModule.parseThreads("-3", 4)).isEqualTo(4);
  }

  @Test
  public void parseThreads_respectsValidValue() {
    assertThat(IndexerModule.parseThreads("8", 4)).isEqualTo(8);
    assertThat(IndexerModule.parseThreads(" 16 ", 4)).isEqualTo(16);
  }
}
```

- [ ] **Step 3: Add the test source to BUILD.bazel if needed**

If the existing `IndexWorkerTest` target uses an explicit `srcs = [...]` list, add the new file. If it uses a glob (e.g., `glob(["src/test/java/**/*.java"])`), no edit is needed.

- [ ] **Step 4: Run the test**

Run:
```bash
bazel test //domains/games/apis/one_d4:IndexerModuleTest
```

(Or whatever target now contains the file — discover via `bazel query 'tests(//domains/games/apis/one_d4/...)'` if unsure.)

Expected: 5 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add domains/games/apis/one_d4/src/test/java/com/muchq/games/one_d4/IndexerModuleTest.java \
        domains/games/apis/one_d4/BUILD.bazel
git commit -m "test(one_d4): cover IndexerModule.parseThreads"
```

---

## Task 8: Format, full test sweep, push

**Files:** none modified directly; this is a verification gate.

- [ ] **Step 1: Format**

Run:
```bash
./scripts/format-all
```

Expected: clean exit. If files are reformatted, stage and amend the relevant prior commit OR create a small `chore: format` commit on top — prefer the latter to avoid amending shared history.

- [ ] **Step 2: Full Bazel test sweep**

Run:
```bash
bazel test //...
```

Expected: all tests PASS. Investigate any failure before pushing.

- [ ] **Step 3: Push the branch**

Run:
```bash
git push -u origin parallel
```

(Or current branch name — confirm with `git branch --show-current` first.)

- [ ] **Step 4: Update the parent doc to mark Option 1 as done**

Edit `domains/games/apis/one_d4/docs/PARALLELIZING_INDEXING.md`. In the "Suggested order of work" list, mark item 2 as complete the same way item 1 already is:

Find:
```
2. **Parallel games within a month** — Fixed thread pool (e.g. 4–8), submit each game to the pool, collect `GameFeature` + occurrences, then batch insert when a batch is full or the month is done. Keeps a single "logical" worker and request ordering; only the CPU part is parallel.
```

Replace with:
```
2. ~~**Parallel games within a month**~~ ✓ — `IndexWorker` now submits each game's extraction to an injected `ExecutorService` (default 4 threads, env `INDEXER_EXTRACTION_THREADS`), drains futures into the existing batch buffer; DB writes remain single-threaded and batched.
```

- [ ] **Step 5: Commit the doc update**

```bash
git add domains/games/apis/one_d4/docs/PARALLELIZING_INDEXING.md
git commit -m "docs(one_d4): mark parallel-extraction step as complete"
git push
```

---

## Notes for the engineer

- **Why an in-order drain rather than `ExecutorCompletionService`?** Order is not load-bearing (rows are keyed by URL, IDs are DB-assigned). The in-order drain is just the simplest possible loop. Test 6 deliberately uses `containsExactlyInAnyOrder` so a future switch to completion-order draining doesn't break it.
- **Why not `parallelStream()`?** It uses the common ForkJoinPool — can't size, can't isolate, can't inject in tests.
- **Pool sizing:** default 4 is conservative. Each concurrent extraction holds ~20 MB of replay state (`IN_PROCESS_MODE.md`), so 4 ≈ 80 MB transient. Don't bump the default without checking memory headroom.
- **Failure mode if `extract` is final:** the test uses an anonymous subclass of `FeatureExtractor` to inject latch/throw behavior. If `FeatureExtractor.extract` is `final`, drop the `final` modifier — the codebase doesn't use Mockito for this test file and the alternative (introducing a mock framework just for these tests) is heavier.
- **Interrupted exception in the drain:** the rewritten loop sets the interrupt flag, logs, and breaks out of the drain. The remaining futures will be cancelled when the executor is shut down by the DI container; not perfect but correct for our use case (we'd be tearing down anyway).
- **`IndexWorker` field is "unused" between Task 2 and Task 4.** That's fine — it lets each commit be small and individually green. Some linters may flag this; tolerate it for the duration of one commit.
