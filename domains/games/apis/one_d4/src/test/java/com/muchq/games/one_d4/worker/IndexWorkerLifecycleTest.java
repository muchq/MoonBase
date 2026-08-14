package com.muchq.games.one_d4.worker;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.GamesResponse;
import com.muchq.games.chess_com_client.Player;
import com.muchq.games.one_d4.api.dto.AggregateRow;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.GameFeatureStore.GameOpening;
import com.muchq.games.one_d4.db.IndexedPeriodStore;
import com.muchq.games.one_d4.db.IndexingRequestDao;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.db.RetentionPolicy;
import com.muchq.games.one_d4.db.TestDb;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.GameReplayer;
import com.muchq.games.one_d4.engine.PgnParser;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.motifs.CheckDetector;
import com.muchq.games.one_d4.queue.InMemoryIndexQueue;
import com.muchq.games.one_d4.queue.IndexMessage;
import com.muchq.games.one_d4.queue.IndexQueue;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.YearMonth;
import java.time.ZoneOffset;
import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

/**
 * The dispatcher itself — the piece #1279 is actually about, and the one that had no test.
 *
 * <p>{@code TableDispatchTest} proves the store can hand a request to any worker. {@code
 * IndexWorkerLifecycle} is what decides to ask, and it owns three behaviours nothing else covers:
 * that a claimed row is turned back into a runnable message with every field intact, that the
 * wake-up nudges it does not read are still consumed, and that it does not care which instance
 * accepted the submit.
 *
 * <p>Driven by calling {@code claimAndRunOne} directly rather than by starting the loop. The loop's
 * own job is the blocking poll and the backoff, both of which are time, and a test that waits on
 * them buys flakiness rather than coverage.
 */
public class IndexWorkerLifecycleTest {

  private static final Instant NOW = Instant.parse("2026-07-01T12:00:00Z");
  private static final Clock CLOCK = Clock.fixed(NOW, ZoneOffset.UTC);

  private TestDb testDb;
  private IndexingRequestDao dao;
  private ExecutorService extraction;

  @BeforeEach
  public void setUp() {
    testDb = TestDb.create("lifecycle");
    dao = new IndexingRequestDao(testDb.jdbi(), CLOCK);
    extraction = Executors.newFixedThreadPool(2);
  }

  @AfterEach
  public void tearDown() {
    extraction.shutdownNow();
  }

  /**
   * The headline at the level that implements it: a row this instance never saw submitted still
   * gets claimed and run.
   */
  @Test
  @Timeout(30)
  public void claimsAndRunsARequestItNeverReceivedAMessageFor() {
    UUID id = submit("elsewhere", false).request().id();

    RecordingClient client = new RecordingClient();
    IndexWorkerLifecycle lifecycle = lifecycleFor(client, new InMemoryIndexQueue());

    assertThat(lifecycle.claimAndRunOne()).as("there was work to take").isTrue();
    assertThat(client.players).containsExactly("elsewhere");
    assertThat(dao.findById(id).orElseThrow().status()).isEqualTo("COMPLETED");
  }

  /** And says so when there is nothing, rather than spinning. */
  @Test
  @Timeout(30)
  public void reportsAnEmptyQueueWhenThereIsNoWork() {
    IndexWorkerLifecycle lifecycle = lifecycleFor(new RecordingClient(), new InMemoryIndexQueue());
    assertThat(lifecycle.claimAndRunOne()).isFalse();
  }

  /**
   * {@code skipCache} has to reach the worker, not merely survive the column.
   *
   * <p>The DAO round-trip is tested next door; this is the other half, and it is the half the
   * column exists for. Nothing else pins it — changing {@code toMessage} to pass {@code false}
   * leaves every other suite green, and the symptom in production would be a request that quietly
   * honours a cache the submitter asked it to bypass, only when it crossed an instance boundary.
   */
  @Test
  @Timeout(30)
  public void carriesSkipCacheFromTheRowIntoTheRun() {
    submit("bypasser", true);

    RecordingClient client = new RecordingClient();
    CachedPeriodStore periods = new CachedPeriodStore();
    lifecycleFor(client, new InMemoryIndexQueue(), periods).claimAndRunOne();

    assertThat(client.players)
        .as("a cached period was honoured for a request that asked to skip the cache")
        .containsExactly("bypasser");
  }

  /** The converse, so the test above is not passing because the cache is simply never consulted. */
  @Test
  @Timeout(30)
  public void honoursACachedPeriodWhenTheRequestDidNotAskToSkipIt() {
    submit("cached", false);

    RecordingClient client = new RecordingClient();
    lifecycleFor(client, new InMemoryIndexQueue(), new CachedPeriodStore()).claimAndRunOne();

    assertThat(client.players).as("the cached month should not have been refetched").isEmpty();
  }

  /**
   * Nudges are consumed after work, not only when idle.
   *
   * <p>This loop is the queue's only consumer and it never reaches the blocking poll while the
   * table has work, so without an explicit drain every accepted submit on a busy instance leaves a
   * message behind forever — and the accumulated backlog of stale nudges then fires back-to-back
   * the moment things go quiet, defeating the idle interval entirely.
   */
  @Test
  @Timeout(30)
  public void drainsPendingNudgesAfterClaimingWork() {
    submit("nudged", false);
    IndexQueue queue = new InMemoryIndexQueue();
    for (int i = 0; i < 5; i++) {
      queue.enqueue(
          new IndexMessage(UUID.randomUUID(), "stale", "CHESS_COM", "2024-01", "2024-01", false));
    }

    assertThat(lifecycleFor(new RecordingClient(), queue).claimAndRunOne()).isTrue();

    assertThat(queue.size())
        .as("stale nudges accumulate forever on an instance that always has work")
        .isZero();
  }

  /**
   * Shutting down hands the work back, rather than letting the JVM drop it on the floor.
   *
   * <p>The poller is a daemon thread, so process exit kills whatever it is running. Without this,
   * every deploy looks exactly like a crash: the row stays owned by a process that no longer
   * exists, nothing may take it until the lease lapses five minutes later, and the attempt spent on
   * claiming it is gone. Three rolling restarts across one long request retire it as poisoned and
   * tell the user its range fails repeatedly — about a fleet that is working perfectly.
   *
   * <p>Handing back rather than waiting for the run to finish is the deliberate choice. An indexing
   * run has no bound — a twelve-month range against a slow chess.com is legitimately long — so a
   * shutdown that drains is a shutdown that hangs. The departing run is not interrupted here; it
   * simply loses the right to write, which the fencing already enforces.
   */
  @Test
  @Timeout(30)
  public void stopHandsBackTheRequestThisProcessWasRunning() throws Exception {
    UUID id = submit("mid-run", false).request().id();
    CountDownLatch reachedFetch = new CountDownLatch(1);
    CountDownLatch letItFinish = new CountDownLatch(1);
    IndexWorkerLifecycle lifecycle =
        lifecycleFor(new BlockingClient(reachedFetch, letItFinish), new InMemoryIndexQueue());

    Thread run = Thread.ofVirtual().start(lifecycle::claimAndRunOne);
    try {
      assertThat(reachedFetch.await(20, TimeUnit.SECONDS)).as("the run got going").isTrue();
      assertThat(dao.findById(id).orElseThrow().attempts()).isEqualTo(1);

      lifecycle.stop();

      assertThat(dao.findById(id).orElseThrow().attempts())
          .as("a deploy must not spend one of the three attempts")
          .isZero();
      assertThat(dao.claim(id, "another-instance", RetentionPolicy.LEASE, NOW))
          .as("the work moves to a surviving instance now, not five minutes from now")
          .isTrue();
    } finally {
      letItFinish.countDown();
      run.join();
    }
  }

  /** Nothing in flight, nothing to give back — shutting down idle must not touch the table. */
  @Test
  @Timeout(30)
  public void stopIsHarmlessWhenNothingIsRunning() {
    UUID id = submit("untouched", false).request().id();

    lifecycleFor(new RecordingClient(), new InMemoryIndexQueue()).stop();

    IndexingRequestStore.IndexingRequest row = dao.findById(id).orElseThrow();
    assertThat(row.status()).isEqualTo("PENDING");
    assertThat(row.attempts()).isZero();
  }

  // --- wiring ---------------------------------------------------------------------------------

  private IndexingRequestStore.Claim submit(String player, boolean skipCache) {
    return dao.createOrAdopt(
        player,
        "CHESS_COM",
        "2024-01",
        "2024-01",
        false,
        skipCache,
        RetentionPolicy.STALE_REQUEST,
        NOW);
  }

  /**
   * The statement timeouts turned a wedged poll from a hang into a throw, which only helps if the
   * loop survives the throw — pollLoop's catch-and-backoff is the designed recovery path, and
   * nothing else pins it. The first claimNext fails the way a timed-out candidate scan now does;
   * the loop must back off and claim the work on a later poll rather than dying with the exception.
   */
  @Test
  @Timeout(30)
  public void pollLoopSurvivesAClaimFailureAndClaimsOnALaterPoll() throws Exception {
    UUID id = submit("resilient", false).request().id();

    FeatureExtractor extractor =
        new FeatureExtractor(new PgnParser(), new GameReplayer(), List.of(new CheckDetector()));
    IndexWorker worker =
        new IndexWorker(
            new RecordingClient(),
            extractor,
            dao,
            new NoOpGameFeatureStore(),
            new NoOpPeriodStore(),
            extraction,
            CLOCK,
            Duration.ofHours(1));
    ThrowingFirstClaimStore store = new ThrowingFirstClaimStore(dao);
    IndexWorkerLifecycle lifecycle =
        new IndexWorkerLifecycle(new InMemoryIndexQueue(), worker, store, CLOCK);
    lifecycle.onApplicationEvent(null);
    try {
      // The recovery poll arrives after ERROR_BACKOFF (5s); poll well past it.
      long deadline = System.nanoTime() + Duration.ofSeconds(25).toNanos();
      while (System.nanoTime() < deadline
          && !"COMPLETED".equals(dao.findById(id).orElseThrow().status())) {
        Thread.sleep(100);
      }
    } finally {
      lifecycle.stop();
    }

    assertThat(store.claimNextCalls.get())
        .as("the loop polled again after the failure — the control that the throw happened")
        .isGreaterThan(1);
    assertThat(dao.findById(id).orElseThrow().status()).isEqualTo("COMPLETED");
  }

  /** Delegates everything to the real DAO except the first claimNext, which fails. */
  private static final class ThrowingFirstClaimStore implements IndexingRequestStore {
    private final IndexingRequestStore delegate;
    final java.util.concurrent.atomic.AtomicInteger claimNextCalls =
        new java.util.concurrent.atomic.AtomicInteger();

    ThrowingFirstClaimStore(IndexingRequestStore delegate) {
      this.delegate = delegate;
    }

    @Override
    public Optional<IndexingRequest> claimNext(String ownerId, Duration lease, Instant now) {
      if (claimNextCalls.incrementAndGet() == 1) {
        throw new IllegalStateException("candidate scan cancelled at the statement timeout");
      }
      return delegate.claimNext(ownerId, lease, now);
    }

    @Override
    public Claim createOrAdopt(
        String player,
        String platform,
        String startMonth,
        String endMonth,
        boolean excludeBullet,
        boolean skipCache,
        Duration lease,
        Instant now) {
      return delegate.createOrAdopt(
          player, platform, startMonth, endMonth, excludeBullet, skipCache, lease, now);
    }

    @Override
    public Optional<IndexingRequest> findById(UUID id) {
      return delegate.findById(id);
    }

    @Override
    public Optional<IndexingRequest> findExistingRequest(
        String player, String platform, String startMonth, String endMonth, boolean excludeBullet) {
      return delegate.findExistingRequest(player, platform, startMonth, endMonth, excludeBullet);
    }

    @Override
    public List<IndexingRequest> listRecent(int limit) {
      return delegate.listRecent(limit);
    }

    @Override
    public void updateStatus(UUID id, String status, String errorMessage, int gamesIndexed) {
      delegate.updateStatus(id, status, errorMessage, gamesIndexed);
    }

    @Override
    public int reclaimStale(Duration staleAfter, Instant now) {
      return delegate.reclaimStale(staleAfter, now);
    }

    @Override
    public boolean claim(UUID id, String ownerId, Duration lease, Instant now) {
      return delegate.claim(id, ownerId, lease, now);
    }

    @Override
    public boolean renewLease(UUID id, String ownerId, Duration lease, Instant now) {
      return delegate.renewLease(id, ownerId, lease, now);
    }

    @Override
    public boolean handBack(UUID id, String ownerId, Instant now) {
      return delegate.handBack(id, ownerId, now);
    }

    @Override
    public boolean releaseOwned(UUID id, String ownerId, Instant now) {
      return delegate.releaseOwned(id, ownerId, now);
    }

    @Override
    public boolean holdsLease(UUID id, String ownerId, Instant now) {
      return delegate.holdsLease(id, ownerId, now);
    }

    @Override
    public boolean updateStatusOwned(
        UUID id,
        String ownerId,
        String status,
        String errorMessage,
        int gamesIndexed,
        Instant now) {
      return delegate.updateStatusOwned(id, ownerId, status, errorMessage, gamesIndexed, now);
    }

    @Override
    public int deleteOlderThan(Instant threshold) {
      return delegate.deleteOlderThan(threshold);
    }
  }

  private IndexWorkerLifecycle lifecycleFor(ChessClient client, IndexQueue queue) {
    return lifecycleFor(client, queue, new NoOpPeriodStore());
  }

  private IndexWorkerLifecycle lifecycleFor(
      ChessClient client, IndexQueue queue, IndexedPeriodStore periods) {
    FeatureExtractor extractor =
        new FeatureExtractor(new PgnParser(), new GameReplayer(), List.of(new CheckDetector()));
    IndexWorker worker =
        new IndexWorker(
            client,
            extractor,
            dao,
            new NoOpGameFeatureStore(),
            periods,
            extraction,
            CLOCK,
            Duration.ofHours(1));
    return new IndexWorkerLifecycle(queue, worker, dao, CLOCK);
  }

  /** Parks inside the fetch so a test can act while a run is genuinely in flight. */
  private static final class BlockingClient extends ChessClient {
    private final CountDownLatch reachedFetch;
    private final CountDownLatch letItFinish;

    BlockingClient(CountDownLatch reachedFetch, CountDownLatch letItFinish) {
      super(null, new ObjectMapper());
      this.reachedFetch = reachedFetch;
      this.letItFinish = letItFinish;
    }

    @Override
    public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
      reachedFetch.countDown();
      try {
        letItFinish.await();
      } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
      }
      return Optional.of(new GamesResponse(List.of()));
    }

    @Override
    public Optional<Player> fetchPlayer(String player) {
      return Optional.empty();
    }
  }

  /** Records which players were actually fetched; each month comes back as an empty archive. */
  private static final class RecordingClient extends ChessClient {
    final Collection<String> players = new ConcurrentLinkedQueue<>();

    RecordingClient() {
      super(null, new ObjectMapper());
    }

    @Override
    public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
      players.add(player);
      return Optional.of(new GamesResponse(List.of()));
    }

    @Override
    public Optional<Player> fetchPlayer(String player) {
      return Optional.empty();
    }
  }

  /** Reports every month as already indexed, so only a skipCache run reaches chess.com. */
  private static final class CachedPeriodStore implements IndexedPeriodStore {
    @Override
    public Optional<IndexedPeriod> findCompletePeriod(
        String player, String platform, String month, boolean excludeBullet) {
      return Optional.of(new IndexedPeriod(player, platform, month, NOW, true, 7, excludeBullet));
    }

    @Override
    public void upsertPeriod(
        String player,
        String platform,
        String month,
        Instant fetchedAt,
        boolean isComplete,
        int gamesCount,
        boolean excludeBullet) {}

    @Override
    public List<IndexedPeriod> findPeriodsForPlayers(Collection<String> players) {
      return List.of();
    }

    @Override
    public int deleteOlderThan(Instant threshold) {
      return 0;
    }
  }

  private static final class NoOpPeriodStore implements IndexedPeriodStore {
    @Override
    public Optional<IndexedPeriod> findCompletePeriod(
        String player, String platform, String month, boolean excludeBullet) {
      return Optional.empty();
    }

    @Override
    public void upsertPeriod(
        String player,
        String platform,
        String month,
        Instant fetchedAt,
        boolean isComplete,
        int gamesCount,
        boolean excludeBullet) {}

    @Override
    public List<IndexedPeriod> findPeriodsForPlayers(Collection<String> players) {
      return List.of();
    }

    @Override
    public int deleteOlderThan(Instant threshold) {
      return 0;
    }
  }

  private static final class NoOpGameFeatureStore implements GameFeatureStore {
    @Override
    public void insertBatch(List<GameFeature> features) {}

    @Override
    public boolean flushOwned(
        UUID requestId,
        String ownerId,
        Instant now,
        List<GameFeature> features,
        Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame) {
      return true;
    }

    @Override
    public int deleteOlderThan(Instant threshold) {
      return 0;
    }

    @Override
    public void insertOccurrencesBatch(
        Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame) {}

    @Override
    public void deleteOccurrencesByGameUrls(List<String> gameUrls) {}

    @Override
    public List<GameFeature> query(Object compiledQuery, int limit, int offset) {
      return List.of();
    }

    @Override
    public List<AggregateRow> aggregate(
        Object compiledQuery, List<String> groupColumns, boolean withOutcomeMetrics, int limit) {
      return List.of();
    }

    @Override
    public AggregateTotals aggregateTotals(Object compiledQuery) {
      return new AggregateTotals(0, 0);
    }

    @Override
    public Map<String, Map<String, List<OccurrenceRow>>> queryOccurrences(List<String> gameUrls) {
      return Map.of();
    }

    @Override
    public java.util.List<GameOpening> fetchOpeningsForRederive(int limit, int offset) {
      return java.util.List.of();
    }

    @Override
    public int updateOpeningFamilies(java.util.List<GameOpening> updates) {
      return 0;
    }

    @Override
    public List<GameForReanalysis> fetchForReanalysis(int limit, int offset) {
      return List.of();
    }
  }
}
