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
import java.util.concurrent.Future;
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
 * what is tested here is that the situation does not arise from a healthy worker being declared
 * dead, and that a worker which really has lost its lease stops.
 *
 * <p>One case is deliberately <em>not</em> covered here, because it is out of a lease's reach: a
 * message still waiting in the queue has no worker to renew for it, so a backlog deeper than the
 * cutoff still retires work that is owned but not yet started. See {@link
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
  public void theLeaseIsRenewedWhileTheWorkerIsInsideASlowArchiveFetch() throws Exception {
    CountDownLatch insideFetch = new CountDownLatch(1);
    CountDownLatch releaseFetch = new CountDownLatch(1);
    CountDownLatch beatDuringFetch = new CountDownLatch(1);

    RenewalRecorder recorder = new RenewalRecorder(beatDuringFetch);
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
            "nothing renewed the lease while the worker sat in the archive fetch — that span is"
                + " unbounded, so a healthy worker outlives its %s lease and is reclaimed mid-run",
            RetentionPolicy.LEASE)
        .isTrue();
  }

  /**
   * Characterizes the hole the heartbeat does <em>not</em> close, so it cannot drift silently.
   *
   * <p>A message waiting in the queue has no worker running for it, so nothing beats on its behalf
   * and {@code updated_at} stays frozen at insert. A backlog deeper than the cutoff therefore
   * retires work that is owned and about to run, and tells the user to re-submit while the message
   * is still queued.
   *
   * <p>Ownership made the consequence worse rather than better, which is worth being explicit
   * about: the worker that eventually dequeues the message now fails to claim the retired row and
   * declines it, so nothing is indexed. Before, it carried on and its unfenced terminal write
   * landed — the user got their games, at the cost of the concurrent-writer corruption that could
   * follow from the freed dedupe slot.
   *
   * <p>This asserts the current, wrong behaviour on purpose. When dispatch claims from this table
   * (#1279) the row will be claimed rather than queued, this test should fail, and it should be
   * rewritten to assert the opposite. That failure is the notification.
   */
  @Test
  public void queuedButUnstartedWorkIsStillRetired_knownGap() {
    TestDb testDb = TestDb.create("liveness_backlog");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID queued = claim(dao).request().id();

    // No worker has picked the message up yet; the backlog outlasts the cutoff.
    clock.advance(SLOW_FETCH);
    dao.reclaimStale(STALE_AFTER, clock.instant());

    assertThat(dao.findById(queued).orElseThrow().status())
        .as(
            "known gap: queued-but-unstarted work is indistinguishable from abandoned work,"
                + " because only a claimed request has an owner to renew for it. Beating on"
                + " enqueue would not fix it either — the process holding the queue is the one"
                + " that may have died. The fix is dispatch claiming from this table (#1279).")
        .isEqualTo("FAILED");
  }

  /** A renewal must not resurrect a request someone else already took. */
  @Test
  public void aRenewalDoesNotReviveARequestThatWasAlreadyRetired() {
    TestDb testDb = TestDb.create("liveness_fenced");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();
    dao.claim(requestId, "worker-a", RetentionPolicy.LEASE, clock.instant());

    clock.advance(SLOW_FETCH);
    dao.reclaimStale(STALE_AFTER, clock.instant());

    assertThat(dao.renewLease(requestId, "worker-a", RetentionPolicy.LEASE, clock.instant()))
        .as("a retired request is not this worker's to keep alive")
        .isFalse();
    assertThat(dao.findById(requestId).orElseThrow().status()).isEqualTo("FAILED");
  }

  /**
   * The fence, end to end: a worker whose lease lapses while it is parked in a fetch must not carry
   * on writing to a row a replacement now owns.
   *
   * <p>Its renewal interval is set past the length of the test, which is what a process that has
   * stopped renewing — paused, partitioned, or dying — looks like from the table's side. When it
   * wakes up it has no way to know it was replaced, so the refusal has to come from the write.
   */
  @Test
  @Timeout(30)
  public void aWorkerWhoseLeaseWasTakenStopsWritingToTheRequestRow() throws Exception {
    TestDb testDb = TestDb.create("liveness_taken");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    CountDownLatch insideFetch = new CountDownLatch(1);
    CountDownLatch releaseFetch = new CountDownLatch(1);
    IndexWorker stalled =
        newWorker(
            new ClockJumpingChessClient(clock, null, insideFetch, releaseFetch),
            dao,
            Duration.ofHours(1));

    Future<?> run = workerExecutor.submit(() -> stalled.process(message(requestId)));
    assertThat(insideFetch.await(15, TimeUnit.SECONDS)).isTrue();

    // Its lease runs out and a replacement takes the row.
    clock.advance(RetentionPolicy.LEASE.plusMinutes(1));
    assertThat(dao.claim(requestId, "replacement", RetentionPolicy.LEASE, clock.instant()))
        .as("an expired lease is available to take")
        .isTrue();

    releaseFetch.countDown();
    run.get(20, TimeUnit.SECONDS);

    assertThat(dao.holdsLease(requestId, "replacement", clock.instant()))
        .as("the replacement still owns the request the stalled worker was writing to")
        .isTrue();
    assertThat(dao.findById(requestId).orElseThrow().status())
        .as("the stalled worker must not stamp its own outcome over the replacement's run")
        .isEqualTo("PROCESSING");
    assertThat(claim(dao).created())
        .as("and the dedupe slot must still be held, not freed by a COMPLETED it had no right to")
        .isFalse();
  }

  /**
   * A lease that lapses with nobody taking it must not abandon the run.
   *
   * <p>The three reasons a fenced write is refused are not the same event. Someone else took the
   * request; the row went terminal; or this worker's own lease expired and no one claimed it. Only
   * the first two mean the range has moved. Collapsing all three into "lost" throws away a run
   * nobody else wants, mid-way, and leaves the row PROCESSING and still holding its dedupe slot
   * until the sweep retires it with a message about an owner that never existed.
   *
   * <p>Reachable without anything being broken: the heartbeat shares one scheduler thread across
   * every in-flight request on the instance, against a five-connection pool with a ten-second
   * timeout, so a stall can outlast a five-minute lease. Here the renewal interval is set past the
   * length of the test to stand in for that, and the clock is pushed past expiry while the worker
   * is parked in the fetch.
   */
  @Test
  @Timeout(30)
  public void aLeaseThatLapsesWithNoTakerIsRenewedRatherThanAbandoned() throws Exception {
    TestDb testDb = TestDb.create("liveness_lapsed");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    CountDownLatch insideFetch = new CountDownLatch(1);
    CountDownLatch releaseFetch = new CountDownLatch(1);
    IndexWorker worker =
        newWorker(
            new ClockJumpingChessClient(clock, null, insideFetch, releaseFetch),
            dao,
            Duration.ofHours(1));

    Future<?> run = workerExecutor.submit(() -> worker.process(message(requestId)));
    assertThat(insideFetch.await(15, TimeUnit.SECONDS)).isTrue();

    // The lease runs out. Nobody claims it.
    clock.advance(RetentionPolicy.LEASE.plusMinutes(1));
    assertThat(dao.holdsLease(requestId, worker.ownerId(), clock.instant()))
        .as("precondition: the lease really has lapsed")
        .isFalse();

    releaseFetch.countDown();
    run.get(20, TimeUnit.SECONDS);

    assertThat(dao.findById(requestId).orElseThrow().status())
        .as("the run finished under a lease it recovered, rather than being abandoned mid-way")
        .isEqualTo("COMPLETED");
  }

  /** Two workers, one request: the second must decline rather than double up on the games. */
  @Test
  @Timeout(30)
  public void aSecondWorkerDeclinesARequestThatIsAlreadyHeld() throws Exception {
    TestDb testDb = TestDb.create("liveness_held");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    CountDownLatch insideFetch = new CountDownLatch(1);
    CountDownLatch releaseFetch = new CountDownLatch(1);
    IndexWorker holder = blockingWorker(dao, insideFetch, releaseFetch);
    workerExecutor.submit(() -> holder.process(message(requestId)));
    assertThat(insideFetch.await(15, TimeUnit.SECONDS)).isTrue();

    // Runs on this thread: it is expected to return at the claim gate without reaching the fetch.
    CountDownLatch secondReachedFetch = new CountDownLatch(1);
    IndexWorker second = blockingWorker(dao, secondReachedFetch, new CountDownLatch(0));
    second.process(message(requestId));

    // Asserted before the holder is released, because finishing its run would release the lease
    // legitimately and the question here is whether the second worker could take it mid-run.
    assertThat(secondReachedFetch.getCount())
        .as("the second worker started indexing a range it does not own")
        .isEqualTo(1);
    assertThat(dao.holdsLease(requestId, second.ownerId(), clock.instant()))
        .as("a second worker must not take a request while its holder's lease is live")
        .isFalse();
    assertThat(dao.holdsLease(requestId, holder.ownerId(), clock.instant())).isTrue();

    releaseFetch.countDown();
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

    // The fetch is hanging and an hour and a half of it goes by. In production the lease has been
    // renewed dozens of times by now; here we wait for one renewal so the assertion is about the
    // sweep's decision rather than about scheduler timing.
    clock.advance(SLOW_FETCH);
    boolean beat = awaitRenewalAtOrAfter(dao, requestId, clock.instant());

    dao.reclaimStale(STALE_AFTER, clock.instant());
    IndexingRequestStore.IndexingRequest row = dao.findById(requestId).orElseThrow();
    releaseFetch.countDown();

    assertThat(beat).as("no renewal landed during the hanging fetch").isTrue();
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
    awaitRenewalAtOrAfter(dao, requestId, clock.instant());
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

  /** Polls until the stored row has been renewed to at least {@code at}, or gives up. */
  private boolean awaitRenewalAtOrAfter(IndexingRequestDao dao, UUID id, Instant at)
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

  /** Trips a latch the first time the worker renews its lease. */
  private static final class RenewalRecorder implements IndexingRequestStore {
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
