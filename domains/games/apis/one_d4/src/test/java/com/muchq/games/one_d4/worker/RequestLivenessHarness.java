package com.muchq.games.one_d4.worker;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.GamesResponse;
import com.muchq.games.chess_com_client.PlayedGame;
import com.muchq.games.chess_com_client.Player;
import com.muchq.games.chess_com_client.PlayerResult;
import com.muchq.games.one_d4.api.dto.AggregateRow;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.IndexedPeriodStore;
import com.muchq.games.one_d4.db.IndexingRequestDao;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.db.RetentionPolicy;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.GameReplayer;
import com.muchq.games.one_d4.engine.PgnParser;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.motifs.CheckDetector;
import com.muchq.games.one_d4.queue.IndexMessage;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.YearMonth;
import java.time.ZoneId;
import java.time.ZoneOffset;
import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.Callable;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;

/**
 * Liveness of an in-flight indexing request: can a request that is being worked on right now be
 * mistaken for an abandoned one?
 *
 * <p>The question used to be answered by inference. A PENDING/PROCESSING row whose {@code
 * updated_at} had not moved in {@link RetentionPolicy#STALE_REQUEST} was presumed dead, which
 * cannot tell a worker that is slow from one that is gone — and status writes are a poor proxy for
 * either, since {@link IndexWorker} wrote PROCESSING only at month boundaries, cache hits, empty
 * archives and 100-game batch flushes, leaving everything between two of those points unobserved.
 *
 * <p>The gap that mattered most was the ordinary one. A single-month request holding fewer than
 * {@code BATCH_SIZE} games flushes exactly once, at the end, so the whole month — the archive
 * fetch, one profile lookup per distinct opponent, and the extraction of every game — sat between
 * two writes. {@code ChessClient} sets no request timeout, so a chess.com call that hangs has no
 * bound at all, and single-month requests are the common case since {@code submitHybrid} runs them
 * inline.
 *
 * <p>#1278 replaced the inference with a claim. A worker takes an explicit lease naming itself and
 * renews it on a timer, so "is the owner still there?" is answered by the owner rather than guessed
 * from its output — a request can run for hours without progress and still be alive, and a crashed
 * one is reclaimable in minutes rather than an hour.
 *
 * <p>Crossing the cutoff is not just a wrong label. Retirement clears {@code dedupe_key}, which is
 * the whole mechanism keeping one live request per range, so a resubmit creates a replacement and
 * two workers index the same games concurrently — interleaving the {@code motif_occurrences}
 * delete/insert pair and doubling every motif count. That is precisely the failure #1249 exists to
 * prevent, reached by a different road. {@code ConcurrentFlushTest} covers the write path itself;
 * what the liveness suites test is that the situation does not arise from a healthy worker being
 * declared dead, and that a worker which really has lost its lease stops.
 *
 * <p>One case is deliberately <em>not</em> covered by them, because it is out of a lease's reach: a
 * message still waiting in the queue has no worker to renew for it, so a backlog deeper than the
 * cutoff still retires work that is owned but not yet started. See {@link
 * RetentionPolicy#STALE_REQUEST}.
 *
 * <p>This harness holds the story above and everything the three suites share — the mutable clock,
 * the misbehaving {@code ChessClient}s, the counting/recording fakes, and the claim and worker
 * builders. The tests split by concern so each runs as its own target: {@link
 * RequestLeaseCeilingTest} (the heartbeat and the run ceiling), {@link
 * RequestInterruptCheckpointTest} (month-boundary interrupt checkpoints), and {@link
 * RequestLeaseOwnershipTest} (ownership under contention).
 */
abstract class RequestLivenessHarness {

  static final String PLAYER = "liveness";

  static final String PLATFORM = "CHESS_COM";

  static final String MONTH = "2024-01";

  static final Instant START = Instant.parse("2026-07-01T12:00:00Z");

  static final Duration STALE_AFTER = RetentionPolicy.STALE_REQUEST;

  /** Comfortably past the cutoff, and well within what an unbounded HTTP call can take. */
  static final Duration SLOW_FETCH = Duration.ofMinutes(90);

  MutableClock clock;

  ExecutorService extractionExecutor;

  ExecutorService workerExecutor;

  @BeforeEach
  public void setUp() {
    clock = new MutableClock(START);
    extractionExecutor = Executors.newFixedThreadPool(2);
    workerExecutor = Executors.newSingleThreadExecutor();
  }

  @AfterEach
  public void tearDown() {
    extractionExecutor.shutdownNow();
    workerExecutor.shutdownNow();
  }

  // --- wiring ---------------------------------------------------------------------------------

  IndexingRequestStore.Claim claim(IndexingRequestDao dao) {
    return dao.createOrAdopt(
        PLAYER, PLATFORM, MONTH, MONTH, false, false, STALE_AFTER, clock.instant());
  }

  IndexMessage message(UUID requestId) {
    return message(requestId, MONTH, MONTH);
  }

  IndexMessage message(UUID requestId, String startMonth, String endMonth) {
    return new IndexMessage(requestId, PLAYER, PLATFORM, startMonth, endMonth, false);
  }

  /** A worker that parks inside the archive fetch until released. */
  IndexWorker blockingWorker(
      IndexingRequestStore store, CountDownLatch entered, CountDownLatch release) {
    return newWorker(
        new ClockJumpingChessClient(clock, null, entered, release), store, Duration.ofMillis(20));
  }

  IndexWorker newWorker(
      ChessClient client, IndexingRequestStore store, Duration heartbeatInterval) {
    return newWorker(
        client,
        store,
        heartbeatInterval,
        new FeatureExtractor(new PgnParser(), new GameReplayer(), List.of(new CheckDetector())),
        new NoOpPeriodStore());
  }

  IndexWorker newWorker(
      ChessClient client,
      IndexingRequestStore store,
      Duration heartbeatInterval,
      FeatureExtractor extractor,
      IndexedPeriodStore periods) {
    return new IndexWorker(
        client,
        extractor,
        store,
        new NoOpGameFeatureStore(),
        periods,
        extractionExecutor,
        clock,
        heartbeatInterval);
  }

  /**
   * A fetch that never returns on its own and never honours an interrupt.
   *
   * <p>The distinction from {@code ClockJumpingChessClient} is the whole point. A fixture that
   * restores the interrupt status hands the run to the nearest checkpoint, which unwinds it before
   * it can attempt a single write — so a cap on {@code fenced()} is unreachable and a test built on
   * one passes whether the cap exists or not. This one absorbs the interrupt entirely, which is
   * what a library that catches {@link InterruptedException} and returns normally looks like from
   * the run's side, and it leaves the run walking on to its terminal write past the ceiling.
   */
  static final class InterruptIgnoringChessClient extends ChessClient {
    private final CountDownLatch entered;
    private final CountDownLatch release;

    InterruptIgnoringChessClient(CountDownLatch entered, CountDownLatch release) {
      super(null, new ObjectMapper());
      this.entered = entered;
      this.release = release;
    }

    @Override
    public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
      entered.countDown();
      while (true) {
        try {
          release.await();
          return Optional.empty();
        } catch (InterruptedException e) {
          // Swallowed and deliberately not restored. Re-awaiting rather than returning is what
          // keeps the run inside the call until the test lets it out.
        }
      }
    }

    @Override
    public Optional<Player> fetchPlayer(String player) {
      return Optional.empty();
    }
  }

  /**
   * A peer that is never coming back: every fetch parks forever, so each run this request gets ends
   * at the ceiling rather than finishing.
   */
  static final class PermanentlyWedgedChessClient extends ChessClient {
    private final AtomicInteger fetches = new AtomicInteger();
    private final CountDownLatch neverReleased = new CountDownLatch(1);

    PermanentlyWedgedChessClient() {
      super(null, new ObjectMapper());
    }

    @Override
    public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
      fetches.incrementAndGet();
      try {
        neverReleased.await();
      } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
      }
      return Optional.empty();
    }

    @Override
    public Optional<Player> fetchPlayer(String player) {
      return Optional.empty();
    }

    boolean awaitFetches(int target) throws InterruptedException {
      for (int i = 0; i < 300; i++) {
        if (fetches.get() >= target) {
          return true;
        }
        Thread.sleep(50);
      }
      return false;
    }
  }

  /** An extraction pool that counts what was handed to it. */
  static final class CountingExecutor extends ThreadPoolExecutor {
    private final AtomicInteger submissions = new AtomicInteger();

    CountingExecutor(int threads) {
      super(threads, threads, 0L, TimeUnit.MILLISECONDS, new LinkedBlockingQueue<>());
    }

    @Override
    public <T> Future<T> submit(Callable<T> task) {
      submissions.incrementAndGet();
      return super.submit(task);
    }

    int submissions() {
      return submissions.get();
    }
  }

  /**
   * A real DAO that counts renewals, so a test can assert the heartbeat <em>stopped</em>.
   *
   * <p>Reading the lease column instead does not work, and the reason is worth recording: between
   * any two beats the stored lease is always momentarily behind a clock that has jumped, so
   * "expired right now" is true even while the heartbeat is beating happily. A test that polls for
   * it wins that race almost immediately and passes against an unbounded run. Counting is the only
   * signal that distinguishes stopped from between-beats.
   */
  static final class CountingDao extends IndexingRequestDao {
    private final AtomicInteger renewals = new AtomicInteger();

    CountingDao(org.jdbi.v3.core.Jdbi jdbi, Clock clock) {
      super(jdbi, clock);
    }

    @Override
    public boolean renewLease(UUID id, String ownerId, Duration lease, Instant now) {
      renewals.incrementAndGet();
      return super.renewLease(id, ownerId, lease, now);
    }

    /**
     * True once renewals have gone quiet for several beat intervals. The worker under test beats
     * every 20ms, so a window of 250ms would hold a dozen of them.
     */
    boolean awaitRenewalsToStop() throws InterruptedException {
      for (int i = 0; i < 20; i++) {
        int before = renewals.get();
        Thread.sleep(250);
        if (renewals.get() == before) {
          return true;
        }
      }
      return false;
    }

    int renewals() {
      return renewals.get();
    }
  }

  /** Polls until the stored row has been renewed to at least {@code at}, or gives up. */
  boolean awaitRenewalAtOrAfter(IndexingRequestDao dao, UUID id, Instant at)
      throws InterruptedException {
    for (int i = 0; i < 200; i++) {
      Optional<IndexingRequestStore.IndexingRequest> row = dao.findById(id);
      if (row.isPresent() && !row.get().updatedAt().isBefore(at)) {
        return true;
      }
      Thread.sleep(25);
    }
    return false;
  }

  /** A clock a test can move, shared by the worker, the DAO, and the assertions. */
  static final class MutableClock extends Clock {
    private final AtomicReference<Instant> now;

    MutableClock(Instant start) {
      this.now = new AtomicReference<>(start);
    }

    void advance(Duration by) {
      now.updateAndGet(current -> current.plus(by));
    }

    @Override
    public Instant instant() {
      return now.get();
    }

    @Override
    public ZoneId getZone() {
      return ZoneOffset.UTC;
    }

    @Override
    public Clock withZone(ZoneId zone) {
      return this;
    }
  }

  /**
   * Stands in for a chess.com call that is slow rather than broken: it either jumps the clock to a
   * fixed instant, or blocks until a latch is released. `ChessClient` has no request timeout, so
   * neither shape is bounded in production.
   */
  static final class ClockJumpingChessClient extends ChessClient {
    private final MutableClock clock;
    private final Instant returnAt;
    private final CountDownLatch entered;
    private final CountDownLatch release;

    ClockJumpingChessClient(
        MutableClock clock, Instant returnAt, CountDownLatch entered, CountDownLatch release) {
      super(null, new ObjectMapper());
      this.clock = clock;
      this.returnAt = returnAt;
      this.entered = entered;
      this.release = release;
    }

    @Override
    public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
      if (entered != null) {
        entered.countDown();
        try {
          release.await(20, TimeUnit.SECONDS);
        } catch (InterruptedException e) {
          Thread.currentThread().interrupt();
        }
      }
      if (returnAt != null) {
        clock.now.set(returnAt);
      }
      // The archive is empty: the month still counts as indexed, and the worker takes its
      // no-games path. What is under test is the silence before this returns, not the payload.
      return Optional.empty();
    }

    @Override
    public Optional<Player> fetchPlayer(String player) {
      return Optional.empty();
    }
  }

  /** Parks in the archive fetch until released, and counts how many runs got that far. */
  static final class CountingChessClient extends ChessClient {
    final java.util.concurrent.atomic.AtomicInteger fetches =
        new java.util.concurrent.atomic.AtomicInteger();
    private final CountDownLatch entered;
    private final CountDownLatch release;

    CountingChessClient(CountDownLatch entered, CountDownLatch release) {
      super(null, new ObjectMapper());
      this.entered = entered;
      this.release = release;
    }

    @Override
    public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
      fetches.incrementAndGet();
      entered.countDown();
      try {
        release.await(20, TimeUnit.SECONDS);
      } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
      }
      return Optional.empty();
    }

    @Override
    public Optional<Player> fetchPlayer(String player) {
      return Optional.empty();
    }
  }

  /**
   * A chess.com call that never comes back on its own, and honours an interrupt the way a JDK
   * {@code HttpClient} call does — by throwing, rather than by quietly returning as if the fetch
   * had succeeded.
   *
   * <p>The latch is never released by anyone. That is the fixture: a peer that has accepted the
   * connection and gone silent produces no bytes, no error and no EOF, so the only way out of the
   * call is an interrupt. The second call through — a retry after the request is requeued — returns
   * normally, so a test can watch the worker recover rather than wedge again.
   */
  static final class WedgedChessClient extends ChessClient {
    private final CountDownLatch entered;
    private final CountDownLatch neverReleased = new CountDownLatch(1);
    private final AtomicInteger fetches = new AtomicInteger();

    WedgedChessClient(CountDownLatch entered) {
      super(null, new ObjectMapper());
      this.entered = entered;
    }

    @Override
    public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
      if (fetches.incrementAndGet() > 1) {
        return Optional.empty();
      }
      entered.countDown();
      try {
        neverReleased.await();
        throw new AssertionError("unreachable: nothing ever counts this latch down");
      } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
        throw new IllegalStateException("interrupted while fetching the archive", e);
      }
    }

    @Override
    public Optional<Player> fetchPlayer(String player) {
      return Optional.empty();
    }
  }

  /**
   * A fetch that swallows its interrupt: it restores the status and returns as if the month were
   * simply empty. The shape of every blocking call this worker does not own.
   */
  static final class SwallowingChessClient extends ChessClient {
    final AtomicInteger fetches = new AtomicInteger();
    private final CountDownLatch entered;
    private final CountDownLatch neverReleased = new CountDownLatch(1);

    SwallowingChessClient(CountDownLatch entered) {
      super(null, new ObjectMapper());
      this.entered = entered;
    }

    @Override
    public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
      fetches.incrementAndGet();
      entered.countDown();
      try {
        neverReleased.await();
      } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
      }
      return Optional.empty();
    }

    @Override
    public Optional<Player> fetchPlayer(String player) {
      return Optional.empty();
    }
  }

  /** One real game in the month, so the run has something to extract and a period to stamp. */
  static final class OneGameChessClient extends ChessClient {
    private static final String PGN =
        """
        [Event "Live Chess"]
        [Site "Chess.com"]
        [White "White"]
        [Black "Black"]
        [Result "1-0"]
        [ECO "C20"]

        1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 1-0
        """;

    OneGameChessClient() {
      super(null, new ObjectMapper());
    }

    @Override
    public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
      return Optional.of(
          new GamesResponse(
              List.of(
                  new PlayedGame(
                      "https://chess.com/g/half-indexed",
                      PGN,
                      Instant.EPOCH,
                      true,
                      null,
                      "",
                      "uuid-1",
                      "",
                      "",
                      "blitz",
                      "chess",
                      new PlayerResult(1500, "win", "https://chess.com/w", "White", "uuid-w"),
                      new PlayerResult(1500, "loss", "https://chess.com/b", "Black", "uuid-b"),
                      "C20"))));
    }

    @Override
    public Optional<Player> fetchPlayer(String player) {
      return Optional.empty();
    }
  }

  /**
   * The archive comes back fine and the <em>profile lookups</em> are what hang — the realistic
   * shape, since they are the calls that fan out. They park on the extraction pool while the run
   * thread waits for them, so an interrupt aimed at the run does not reach them.
   */
  static final class WedgedProfileChessClient extends ChessClient {
    final CountDownLatch lookupInterrupted = new CountDownLatch(1);
    private final CountDownLatch entered;
    private final CountDownLatch neverReleased = new CountDownLatch(1);

    WedgedProfileChessClient(CountDownLatch entered) {
      super(null, new ObjectMapper());
      this.entered = entered;
    }

    @Override
    public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
      return new OneGameChessClient().fetchGames(player, yearMonth);
    }

    @Override
    public Optional<Player> fetchPlayer(String player) {
      entered.countDown();
      try {
        neverReleased.await();
      } catch (InterruptedException e) {
        // Report the interrupt rather than absorb it. Recorded here because the claim is
        // precisely that this thread got one.
        lookupInterrupted.countDown();
        Thread.currentThread().interrupt();
      }
      return Optional.empty();
    }
  }

  /**
   * Extraction that parks on the pool, leaving the run thread waiting in {@code future.get()} —
   * which is where an interrupt aimed at the run lands when the month is mid-extraction.
   */
  static final class BlockingFeatureExtractor extends FeatureExtractor {
    private final CountDownLatch entered;
    private final CountDownLatch release;

    BlockingFeatureExtractor(CountDownLatch entered, CountDownLatch release) {
      super(new PgnParser(), new GameReplayer(), List.of(new CheckDetector()));
      this.entered = entered;
      this.release = release;
    }

    @Override
    public GameFeatures extract(String pgn) {
      entered.countDown();
      try {
        release.await(20, TimeUnit.SECONDS);
      } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
      }
      return super.extract(pgn);
    }
  }

  /** Counts period writes, which is the thing an interrupted run must not have made. */
  static final class RecordingPeriodStore implements IndexedPeriodStore {
    final AtomicInteger upserts = new AtomicInteger();

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
        boolean excludeBullet) {
      upserts.incrementAndGet();
    }

    @Override
    public List<IndexedPeriod> findPeriodsForPlayers(Collection<String> players) {
      return List.of();
    }

    @Override
    public int deleteOlderThan(Instant threshold) {
      return 0;
    }
  }

  /** Parks in the archive fetch until released, then fails the run for real. */
  static final class ThrowingChessClient extends ChessClient {
    private final CountDownLatch entered;
    private final CountDownLatch release;

    ThrowingChessClient(CountDownLatch entered, CountDownLatch release) {
      super(null, new ObjectMapper());
      this.entered = entered;
      this.release = release;
    }

    @Override
    public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
      entered.countDown();
      try {
        release.await(20, TimeUnit.SECONDS);
      } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
      }
      throw new IllegalStateException("chess.com exploded");
    }

    @Override
    public Optional<Player> fetchPlayer(String player) {
      return Optional.empty();
    }
  }

  /** Trips a latch the first time the worker renews its lease. */
  static final class RenewalRecorder implements IndexingRequestStore {
    private final CountDownLatch renewed;

    RenewalRecorder(CountDownLatch renewed) {
      this.renewed = renewed;
    }

    @Override
    public boolean claim(UUID id, String ownerId, Duration lease, Instant now) {
      return true;
    }

    @Override
    public boolean renewLease(UUID id, String ownerId, Duration lease, Instant now) {
      renewed.countDown();
      return true;
    }

    @Override
    public boolean handBack(UUID id, String ownerId, Instant now) {

      return false;
    }

    @Override
    public boolean releaseOwned(UUID id, String ownerId, Instant now) {

      return false;
    }

    @Override
    public boolean holdsLease(UUID id, String ownerId, Instant now) {
      return true;
    }

    @Override
    public boolean updateStatusOwned(
        UUID id,
        String ownerId,
        String status,
        String errorMessage,
        int gamesIndexed,
        Instant now) {
      return true;
    }

    @Override
    public void updateStatus(UUID id, String status, String errorMessage, int gamesIndexed) {}

    @Override
    public Claim createOrAdopt(
        String player,
        String platform,
        String startMonth,
        String endMonth,
        boolean excludeBullet,
        boolean skipCache,
        Duration staleAfter,
        Instant now) {
      throw new UnsupportedOperationException();
    }

    /** Not exercised by RequestLiveness tests: nothing here dispatches from the table. */
    @Override
    public Optional<IndexingRequest> claimNext(String ownerId, Duration lease, Instant now) {
      return Optional.empty();
    }

    @Override
    public Optional<IndexingRequest> findById(UUID id) {
      return Optional.empty();
    }

    @Override
    public Optional<IndexingRequest> findExistingRequest(
        String player, String platform, String startMonth, String endMonth, boolean excludeBullet) {
      return Optional.empty();
    }

    @Override
    public List<IndexingRequest> listRecent(int limit) {
      return List.of();
    }

    @Override
    public int reclaimStale(Duration staleAfter, Instant now) {
      return 0;
    }

    @Override
    public int deleteOlderThan(Instant threshold) {
      return 0;
    }
  }

  static final class NoOpPeriodStore implements IndexedPeriodStore {
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

  static final class NoOpGameFeatureStore implements GameFeatureStore {
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
        Object compiledQuery, List<String> groupColumns, int limit) {
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
    public List<GameForReanalysis> fetchForReanalysis(int limit, int offset) {
      return List.of();
    }
  }
}
