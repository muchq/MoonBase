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
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

/**
 * Liveness of an in-flight indexing request: can a request that is being worked on right now be
 * mistaken for an abandoned one?
 *
 * <p>{@link RetentionPolicy#STALE_REQUEST} retires a PENDING/PROCESSING request whose {@code
 * updated_at} has not moved in an hour, on the theory that its owner is dead. That theory is only
 * as good as the worker's heartbeat, and status writes alone are not one: {@link IndexWorker} wrote
 * PROCESSING at the start of a run and again at each month boundary, cache hit, empty archive and
 * 100-game batch flush, leaving everything between two of those points unobserved.
 *
 * <p>The gap that mattered most was the ordinary one. A single-month request holding fewer than
 * {@code BATCH_SIZE} games flushes exactly once, at the end, so the whole month — the archive
 * fetch, one profile lookup per distinct opponent, and the extraction of every game — sat between
 * two writes. {@code ChessClient} sets no request timeout, so a chess.com call that hangs has no
 * bound at all, and single-month requests are the common case since {@code submitHybrid} runs them
 * inline.
 *
 * <p>Crossing the cutoff is not just a wrong label. Retirement clears {@code dedupe_key}, which is
 * the whole mechanism keeping one live request per range, so a resubmit creates a replacement and
 * two workers index the same games concurrently — interleaving the {@code motif_occurrences}
 * delete/insert pair and doubling every motif count. That is precisely the failure #1249 exists to
 * prevent, reached by a different road.
 *
 * <p>These tests were written against the defect and observed failing before the timer-based
 * heartbeat that now satisfies them. What they pin is the invariant, not the fix: any future change
 * that lets a running request go unobserved for longer than the cutoff breaks them.
 *
 * <p>One case is deliberately <em>not</em> covered here, because it is out of the heartbeat's
 * reach: a message still waiting in the queue has no worker to beat for it, so a backlog deeper
 * than the cutoff still retires work that is owned but not yet started. See {@link
 * RetentionPolicy#STALE_REQUEST}.
 */
public class RequestLivenessTest {

  private static final String PLAYER = "liveness";
  private static final String PLATFORM = "CHESS_COM";
  private static final String MONTH = "2024-01";
  private static final Instant START = Instant.parse("2026-07-01T12:00:00Z");
  private static final Duration STALE_AFTER = RetentionPolicy.STALE_REQUEST;

  /** Comfortably past the cutoff, and well within what an unbounded HTTP call can take. */
  private static final Duration SLOW_FETCH = Duration.ofMinutes(90);

  private MutableClock clock;
  private ExecutorService extractionExecutor;
  private ExecutorService workerExecutor;

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

  /**
   * The property, stated directly: a request must be touched while it is inside the archive fetch,
   * not only on either side of it. That span has no checkpoint and no HTTP timeout bounding it, so
   * if nothing writes during it, "stale" means "slow" as well as "dead".
   *
   * <p>Wall-clock, not the injected clock. Advancing a fake clock proves the arithmetic; it cannot
   * prove that something in this process is still running, which is the only thing a heartbeat is
   * for. The interval is shrunk to milliseconds so a beat is observable without waiting a quarter
   * of an hour for one.
   */
  @Test
  @Timeout(30)
  public void theRequestIsTouchedWhileTheWorkerIsInsideASlowArchiveFetch() throws Exception {
    CountDownLatch insideFetch = new CountDownLatch(1);
    CountDownLatch releaseFetch = new CountDownLatch(1);
    CountDownLatch beatDuringFetch = new CountDownLatch(1);

    HeartbeatRecorder recorder = new HeartbeatRecorder(beatDuringFetch);
    IndexWorker worker =
        newWorker(
            new ClockJumpingChessClient(clock, null, insideFetch, releaseFetch),
            recorder,
            Duration.ofMillis(20));

    workerExecutor.submit(() -> worker.process(message(UUID.randomUUID())));
    assertThat(insideFetch.await(15, TimeUnit.SECONDS))
        .as("worker should have reached the archive fetch")
        .isTrue();

    boolean touched = beatDuringFetch.await(10, TimeUnit.SECONDS);
    releaseFetch.countDown();

    assertThat(touched)
        .as(
            "nothing refreshed the request while the worker sat in the archive fetch — that span"
                + " is unbounded, so a healthy worker crosses the %s cutoff and is retired mid-run",
            STALE_AFTER)
        .isTrue();
  }

  /** A beat must not resurrect a request someone else already took. */
  @Test
  public void aHeartbeatDoesNotReviveARequestThatWasAlreadyRetired() {
    TestDb testDb = TestDb.create("liveness_fenced");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    clock.advance(SLOW_FETCH);
    dao.reclaimStale(STALE_AFTER, clock.instant());

    assertThat(dao.heartbeat(requestId, clock.instant()))
        .as("a retired request is not this worker's to keep alive")
        .isFalse();
    assertThat(dao.findById(requestId).orElseThrow().status()).isEqualTo("FAILED");
  }

  /**
   * The same thing observed from the sweep's side, against a real database and a worker genuinely
   * parked inside {@code fetchGames}. A request being worked on must survive a retention pass.
   */
  @Test
  @Timeout(30)
  public void aRequestIsNotRetiredWhileItsWorkerIsMidFlight() throws Exception {
    TestDb testDb = TestDb.create("liveness_midflight");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    CountDownLatch insideFetch = new CountDownLatch(1);
    CountDownLatch releaseFetch = new CountDownLatch(1);
    IndexWorker worker = blockingWorker(dao, insideFetch, releaseFetch);

    workerExecutor.submit(() -> worker.process(message(requestId)));
    assertThat(insideFetch.await(15, TimeUnit.SECONDS))
        .as("worker should have reached the archive fetch")
        .isTrue();

    // The fetch is hanging and an hour and a half of it goes by. In production the heartbeat has
    // beaten dozens of times by now; here we wait for one so the assertion is about the sweep's
    // decision rather than about scheduler timing.
    clock.advance(SLOW_FETCH);
    boolean beat = awaitHeartbeatAtOrAfter(dao, requestId, clock.instant());

    dao.reclaimStale(STALE_AFTER, clock.instant());
    IndexingRequestStore.IndexingRequest row = dao.findById(requestId).orElseThrow();
    releaseFetch.countDown();

    assertThat(beat).as("no heartbeat landed during the hanging fetch").isTrue();
    assertThat(row.status())
        .as("a request whose worker is still running must not be retired as abandoned")
        .isIn("PENDING", "PROCESSING");
  }

  /**
   * The consequence, and the reason the label matters. Retirement frees the dedupe slot; if it can
   * happen to a live request, a resubmit starts a rival worker over the same games and the
   * occurrence delete/insert pair interleaves — the doubled motif counts this whole change exists
   * to prevent.
   */
  @Test
  @Timeout(30)
  public void theDedupeSlotIsNotFreedWhileAWorkerIsMidFlight() throws Exception {
    TestDb testDb = TestDb.create("liveness_slot");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    CountDownLatch insideFetch = new CountDownLatch(1);
    CountDownLatch releaseFetch = new CountDownLatch(1);
    IndexWorker worker = blockingWorker(dao, insideFetch, releaseFetch);

    workerExecutor.submit(() -> worker.process(message(requestId)));
    assertThat(insideFetch.await(15, TimeUnit.SECONDS)).isTrue();

    clock.advance(SLOW_FETCH);
    awaitHeartbeatAtOrAfter(dao, requestId, clock.instant());
    dao.reclaimStale(STALE_AFTER, clock.instant());

    IndexingRequestStore.Claim resubmit = claim(dao);
    releaseFetch.countDown();

    assertThat(resubmit.created())
        .as(
            "resubmitting a range that is actively being indexed must adopt the running request,"
                + " not start a rival one over the same games")
        .isFalse();
    assertThat(resubmit.request().id()).isEqualTo(requestId);
  }

  // --- wiring ---------------------------------------------------------------------------------

  private IndexingRequestStore.Claim claim(IndexingRequestDao dao) {
    return dao.createOrAdopt(PLAYER, PLATFORM, MONTH, MONTH, false, STALE_AFTER, clock.instant());
  }

  private IndexMessage message(UUID requestId) {
    return new IndexMessage(requestId, PLAYER, PLATFORM, MONTH, MONTH, false);
  }

  /** A worker that parks inside the archive fetch until released. */
  private IndexWorker blockingWorker(
      IndexingRequestStore store, CountDownLatch entered, CountDownLatch release) {
    return newWorker(
        new ClockJumpingChessClient(clock, null, entered, release), store, Duration.ofMillis(20));
  }

  private IndexWorker newWorker(
      ChessClient client, IndexingRequestStore store, Duration heartbeatInterval) {
    FeatureExtractor extractor =
        new FeatureExtractor(new PgnParser(), new GameReplayer(), List.of(new CheckDetector()));
    return new IndexWorker(
        client,
        extractor,
        store,
        new NoOpGameFeatureStore(),
        new NoOpPeriodStore(),
        extractionExecutor,
        clock,
        heartbeatInterval);
  }

  /** Polls until the stored row has been refreshed to at least {@code at}, or gives up. */
  private boolean awaitHeartbeatAtOrAfter(IndexingRequestDao dao, UUID id, Instant at)
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
  private static final class MutableClock extends Clock {
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
  private static final class ClockJumpingChessClient extends ChessClient {
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

  /** Trips a latch the first time anything refreshes the request. */
  private static final class HeartbeatRecorder implements IndexingRequestStore {
    private final CountDownLatch touched;

    HeartbeatRecorder(CountDownLatch touched) {
      this.touched = touched;
    }

    @Override
    public boolean heartbeat(UUID id, Instant now) {
      touched.countDown();
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
        Duration staleAfter,
        Instant now) {
      throw new UnsupportedOperationException();
    }

    @Override
    public Optional<IndexingRequest> findById(UUID id) {
      return Optional.empty();
    }

    @Override
    public Optional<IndexingRequest> findExistingRequest(
        String player,
        String platform,
        String startMonth,
        String endMonth,
        boolean excludeBullet,
        Duration staleAfter,
        Instant now) {
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
