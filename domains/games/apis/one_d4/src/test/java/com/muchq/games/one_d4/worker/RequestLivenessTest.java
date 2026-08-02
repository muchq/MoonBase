package com.muchq.games.one_d4.worker;

import static org.assertj.core.api.Assertions.assertThat;

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
import com.muchq.games.one_d4.db.TestDb;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.GameReplayer;
import com.muchq.games.one_d4.engine.PgnParser;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.motifs.CheckDetector;
import com.muchq.games.one_d4.queue.InMemoryIndexQueue;
import com.muchq.games.one_d4.queue.IndexMessage;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.YearMonth;
import java.time.ZoneId;
import java.time.ZoneOffset;
import java.util.ArrayList;
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
   * The tripwire from #1278, now tripped, and honoured rather than renamed.
   *
   * <p>It used to assert that queued-but-unstarted work was retired, and said so as a known gap:
   * "when dispatch claims from this table (#1279) the row will be claimed rather than queued, this
   * test should fail, and it should be rewritten to assert the opposite."
   *
   * <p>So: a request nobody has started is not abandoned, it is queued, and any worker may take it.
   * What still ends it is not age but a fleet that is demonstrably not running anything — the
   * distinction between "my turn has not come" and "nothing is serving this", which age alone could
   * never make.
   */
  @Test
  public void queuedButUnstartedWorkIsWaitingForAWorker() {
    TestDb testDb = TestDb.create("liveness_backlog");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID queued = claim(dao).request().id();

    // A worker is busy elsewhere: the fleet is alive, this row's turn has not come.
    IndexingRequestStore.Claim elsewhere =
        dao.createOrAdopt(
            "other", PLATFORM, MONTH, MONTH, false, false, STALE_AFTER, clock.instant());
    dao.claim(elsewhere.request().id(), "busy-worker", RetentionPolicy.LEASE, clock.instant());

    clock.advance(SLOW_FETCH);
    dao.renewLease(elsewhere.request().id(), "busy-worker", RetentionPolicy.LEASE, clock.instant());
    dao.reclaimStale(STALE_AFTER, clock.instant());

    assertThat(dao.findById(queued).orElseThrow().status())
        .as("waiting behind a working fleet is not abandonment")
        .isIn("PENDING", "PROCESSING");
    assertThat(dao.claimNext("free-worker", RetentionPolicy.LEASE, clock.instant()))
        .as("and any worker can take it")
        .hasValueSatisfying(r -> assertThat(r.id()).isEqualTo(queued));
  }

  // --- #1282: the ceiling on a single run -----------------------------------------------------

  /**
   * The case the lease cannot reach on its own: a worker that is alive but not progressing.
   *
   * <p>Every reclamation arm needs the owner to have <em>stopped</em>, and a wedged run has not.
   * Its heartbeat is a separate thread, so a run blocked forever on a hung socket read renews
   * forever: the release arm sees an unexpired lease, the poison counter never moves because
   * nothing re-claims it, and the stalled arm sees a live lease. No better liveness probe can fix
   * that — the worker genuinely is alive. What is unknown is whether the <em>run</em> is going
   * anywhere, and only a clock can answer that.
   *
   * <p>So renewals are bounded. Past the ceiling the heartbeat stops, the lease lapses within
   * {@link RetentionPolicy#LEASE}, and the ordinary release path takes the request somewhere it can
   * actually be worked on.
   */
  @Test
  @Timeout(30)
  public void aRunThatOutlivesTheCeilingLetsGoOfItsRequest() throws Exception {
    TestDb testDb = TestDb.create("liveness_ceiling");
    CountingDao dao = new CountingDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    CountDownLatch insideFetch = new CountDownLatch(1);
    CountDownLatch releaseFetch = new CountDownLatch(1);
    IndexWorker wedged = blockingWorker(dao, insideFetch, releaseFetch);
    workerExecutor.submit(() -> wedged.process(message(requestId)));
    assertThat(insideFetch.await(15, TimeUnit.SECONDS)).isTrue();

    // Still under the ceiling: the heartbeat is doing its job and nobody else may take this.
    clock.advance(RetentionPolicy.MAX_RUN.dividedBy(2));
    assertThat(awaitRenewalAtOrAfter(dao, requestId, clock.instant()))
        .as("a long run is still a live one")
        .isTrue();
    assertThat(dao.claimNext("rescuer", RetentionPolicy.LEASE, clock.instant()))
        .as("a run under the ceiling must not be interrupted")
        .isEmpty();

    clock.advance(RetentionPolicy.MAX_RUN);
    boolean stopped = dao.awaitRenewalsToStop();
    boolean stillHeld = dao.holdsLease(requestId, wedged.ownerId(), clock.instant());
    Optional<IndexingRequestStore.IndexingRequest> rescued =
        dao.claimNext("rescuer", RetentionPolicy.LEASE, clock.instant());
    releaseFetch.countDown();

    assertThat(stopped).as("the heartbeat must stop once the run outlives the ceiling").isTrue();
    assertThat(stillHeld).as("and the lease it was holding open must lapse").isFalse();
    assertThat(rescued)
        .as("so the work can move to a worker that will actually get through it")
        .hasValueSatisfying(r -> assertThat(r.id()).isEqualTo(requestId));
  }

  /**
   * And it must not renew its way back in when it wakes up.
   *
   * <p>{@code fenced()} renews too — deliberately, because a lapsed-but-unstolen lease normally
   * means a stalled pool rather than a lost request, and abandoning the run there would throw away
   * work nobody else wants. That recovery is exactly wrong past the ceiling: the run whose renewals
   * were cut would restore its own lease at its next write and carry on holding the range, which
   * makes the ceiling decorative. Capping only the heartbeat looks like it works and does not.
   */
  @Test
  @Timeout(30)
  public void aRunPastTheCeilingCannotRenewItsWayBackInAtTheNextWrite() throws Exception {
    TestDb testDb = TestDb.create("liveness_ceiling_fenced");
    CountingDao dao = new CountingDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    CountDownLatch insideFetch = new CountDownLatch(1);
    CountDownLatch releaseFetch = new CountDownLatch(1);
    // Deliberately a fetch that ignores the interrupt rather than one that restores it. A fixture
    // that restores the status hands the run to the nearest checkpoint, which unwinds it before it
    // reaches a single fenced write — so the test passes with the cap deleted, which is exactly
    // what this one was doing until the panel caught it.
    IndexWorker wedged =
        newWorker(
            new InterruptIgnoringChessClient(insideFetch, releaseFetch),
            dao,
            Duration.ofMillis(20));
    Future<?> run = workerExecutor.submit(() -> wedged.process(message(requestId)));
    assertThat(insideFetch.await(15, TimeUnit.SECONDS)).isTrue();

    clock.advance(RetentionPolicy.MAX_RUN.plus(RetentionPolicy.LEASE));
    assertThat(dao.awaitRenewalsToStop()).as("the ceiling should have fired").isTrue();
    // Nobody has taken the row, so owner_id still names this worker — which is exactly what makes
    // fenced()'s recovery renewal succeed if it is not also capped.
    int renewalsAtCeiling = dao.renewals();

    // The fetch finally returns, with no interrupt for the run to notice, and the run walks on to
    // its terminal write under a lapsed lease it still owns.
    releaseFetch.countDown();
    run.get(15, TimeUnit.SECONDS);

    assertThat(dao.renewals())
        .as("fenced() must not renew a run back in past its own ceiling")
        .isEqualTo(renewalsAtCeiling);
    assertThat(dao.holdsLease(requestId, wedged.ownerId(), clock.instant()))
        .as("a run past the ceiling must not reassert the lease its own ceiling took away")
        .isFalse();
    assertThat(dao.findById(requestId).orElseThrow().status())
        .as("and must not finish the request it no longer holds")
        .isNotEqualTo("COMPLETED");
  }

  /**
   * The blast radius, which is what makes this worth fixing rather than tolerating.
   *
   * <p>The stalled arm asks whether any worker anywhere holds a live lease, so one wedged run does
   * not merely strand its own range — it vouches for the whole fleet. Every other queued request
   * stays PENDING in silence for as long as the wedge lasts, however dead everything else is. The
   * ceiling puts a bound on that: once the wedged lease lapses, the table tells the truth again and
   * the users waiting behind it get an answer.
   */
  @Test
  @Timeout(30)
  public void aWedgedRunStopsVouchingForADeadFleet() throws Exception {
    TestDb testDb = TestDb.create("liveness_ceiling_fleet");
    CountingDao dao = new CountingDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    IndexingRequestStore.Claim queued =
        dao.createOrAdopt(
            "waiting", PLATFORM, MONTH, MONTH, false, false, STALE_AFTER, clock.instant());

    CountDownLatch insideFetch = new CountDownLatch(1);
    CountDownLatch releaseFetch = new CountDownLatch(1);
    IndexWorker wedged = blockingWorker(dao, insideFetch, releaseFetch);
    workerExecutor.submit(() -> wedged.process(message(requestId)));
    assertThat(insideFetch.await(15, TimeUnit.SECONDS)).isTrue();

    clock.advance(RetentionPolicy.MAX_RUN.plus(RetentionPolicy.LEASE));
    assertThat(dao.awaitRenewalsToStop()).isTrue();
    dao.reclaimStale(STALE_AFTER, clock.instant());
    String queuedStatus = dao.findById(queued.request().id()).orElseThrow().status();
    releaseFetch.countDown();

    assertThat(queuedStatus)
        .as("nothing has run for hours, and the wedged lease must stop hiding that")
        .isEqualTo("FAILED");
  }

  /**
   * The converse, and the reason the arm above is qualified rather than a plain age check: a run
   * that is merely slow must go on vouching for the fleet.
   *
   * <p>Without this the test above passes against a stalled arm that ignores leases altogether,
   * which would retire every queued request behind any long-running one. The clock moves past
   * {@link RetentionPolicy#STALE_REQUEST} — so the queued sibling is old enough to be eligible on
   * age — but not past the ceiling, so the heartbeat is still beating and the lease is still live.
   */
  @Test
  public void aSlowRunUnderTheCeilingKeepsVouchingForTheFleet() throws Exception {
    TestDb testDb = TestDb.create("liveness_ceiling_fleet_live");
    CountingDao dao = new CountingDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    IndexingRequestStore.Claim queued =
        dao.createOrAdopt(
            "waiting", PLATFORM, MONTH, MONTH, false, false, STALE_AFTER, clock.instant());

    CountDownLatch insideFetch = new CountDownLatch(1);
    CountDownLatch releaseFetch = new CountDownLatch(1);
    IndexWorker slow = blockingWorker(dao, insideFetch, releaseFetch);
    workerExecutor.submit(() -> slow.process(message(requestId)));
    assertThat(insideFetch.await(15, TimeUnit.SECONDS)).isTrue();

    clock.advance(STALE_AFTER.plus(Duration.ofMinutes(1)));
    assertThat(awaitRenewalAtOrAfter(dao, requestId, clock.instant().minus(RetentionPolicy.LEASE)))
        .as("the heartbeat is still beating, so this run still holds a live lease")
        .isTrue();

    dao.reclaimStale(STALE_AFTER, clock.instant());
    String queuedStatus = dao.findById(queued.request().id()).orElseThrow().status();
    releaseFetch.countDown();

    assertThat(queuedStatus)
        .as("a slow run is a backlog, not an outage: the queue behind it must keep waiting")
        .isEqualTo("PENDING");
  }

  // --- #1282 option 2: cutting the wedged run loose
  // ----------------------------------------------------

  /**
   * Giving the request up recovers the row. This is the other half: recovering the thread.
   *
   * <p>The ceiling on its own leaves the wedged run exactly where it was — parked in a call that
   * will never return, holding the poller, which is one thread. So the instance stops taking work
   * from the moment of the wedge and does not start again until someone restarts it. A fleet that
   * recovers each stranded row by shedding the worker that was on it is not recovering; it is
   * trading one stuck request for one dead instance, and the second is the more expensive.
   *
   * <p>Nothing releases the fetch in this test. Its latch is never counted down, exactly as a hung
   * socket is never answered, so the run returning at all is the assertion — and before the
   * interrupt existed it did not return, which is why the ceiling tests above all have to release
   * their latch by hand at the end.
   */
  @Test
  @Timeout(60)
  public void aRunPastTheCeilingIsCutLooseFromTheCallItIsStuckIn() throws Exception {
    TestDb testDb = TestDb.create("liveness_ceiling_interrupt");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    CountDownLatch insideFetch = new CountDownLatch(1);
    IndexWorker worker = newWorker(new WedgedChessClient(insideFetch), dao, Duration.ofMillis(20));
    Future<?> run = workerExecutor.submit(() -> worker.process(message(requestId)));
    assertThat(insideFetch.await(15, TimeUnit.SECONDS)).isTrue();

    clock.advance(RetentionPolicy.MAX_RUN.plus(RetentionPolicy.LEASE));
    run.get(40, TimeUnit.SECONDS);

    // A renewal already in flight when the ceiling fired stamped an expiry from the clock as it
    // then stood. Move past that too, so what follows is about the heartbeat having stopped rather
    // than about which side of one beat the test happened to land on.
    clock.advance(RetentionPolicy.LEASE.plusMinutes(1));

    IndexingRequestStore.IndexingRequest row = dao.findById(requestId).orElseThrow();
    assertThat(row.status())
        .as(
            "a run that was told to stop did not fail — the range is fine, and a FAILED here would"
                + " spend an attempt and blame the user's request for the worker being stuck")
        .isEqualTo("PROCESSING");
    assertThat(row.errorMessage()).isNull();
    assertThat(dao.claimNext("rescuer", RetentionPolicy.LEASE, clock.instant()))
        .as("and the range is back in the queue for a worker that is not wedged")
        .hasValueSatisfying(r -> assertThat(r.id()).isEqualTo(requestId));
  }

  /**
   * The consequence at the level a deployment feels it: the instance keeps working.
   *
   * <p>Driven through the poll loop rather than through {@code process}, because the loop is what
   * the claim is about — {@code claimAndRunOne} in a straight round is exactly what {@code
   * pollLoop} does with a backlog. The first request wedges and is cut loose at the ceiling; what
   * has to happen next is that the same thread goes back and takes the next one.
   *
   * <p>Two things could stop it and both are silent. The run might never return, which is the
   * ceiling's half. Or it might return with the interrupt status still set — the interrupt was
   * aimed at a run, not at the poller that outlives it — and then every request the poller takes
   * afterwards dies at its first checkpoint, leaving an instance that looks like it is polling and
   * completes nothing.
   */
  @Test
  @Timeout(60)
  public void theWorkerGoesBackToTakingRequestsAfterAWedgedRunIsCutLoose() throws Exception {
    TestDb testDb = TestDb.create("liveness_ceiling_slot");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID wedgedId = claim(dao).request().id();
    clock.advance(Duration.ofMinutes(1)); // so the queue order is the one the narrative assumes
    UUID nextId =
        dao.createOrAdopt(
                "next-in-line", PLATFORM, MONTH, MONTH, false, false, STALE_AFTER, clock.instant())
            .request()
            .id();

    CountDownLatch insideFetch = new CountDownLatch(1);
    // Wedged once. A permanently hung peer would wedge the retry too and prove nothing about the
    // poller, and the interesting claim is about the instance recovering, not the request.
    IndexWorker worker = newWorker(new WedgedChessClient(insideFetch), dao, Duration.ofMillis(20));
    IndexWorkerLifecycle lifecycle =
        new IndexWorkerLifecycle(new InMemoryIndexQueue(), worker, dao, clock);

    Future<?> poller =
        workerExecutor.submit(
            () -> {
              while (lifecycle.claimAndRunOne()) {
                // Straight round for the next request, as pollLoop does when there is a backlog.
              }
            });

    assertThat(insideFetch.await(15, TimeUnit.SECONDS)).isTrue();
    clock.advance(RetentionPolicy.MAX_RUN.plus(RetentionPolicy.LEASE));
    poller.get(40, TimeUnit.SECONDS);

    assertThat(dao.findById(nextId).orElseThrow().status())
        .as("the queue behind the wedge never moved: this worker never came back for it")
        .isEqualTo("COMPLETED");
    assertThat(dao.findById(wedgedId).orElseThrow().status())
        .as("and the request that was stuck was picked up again and finished")
        .isEqualTo("COMPLETED");
  }

  /**
   * A wedge that never clears has to retire the request, not loop on it.
   *
   * <p>Option 2 is what makes this reachable: before it, a wedged run never returned, so the poller
   * never came back for the row. Now it does — and it is the same process, presenting the same
   * token to a {@link IndexingRequestStore#claim} that deliberately holds {@code attempts} flat for
   * its own holder so a run can renew across its own retries. Leave the row owned on the way out
   * and that kindness becomes the bug: the poller re-claims its own abandoned request every {@link
   * RetentionPolicy#LEASE}, takes a fresh ceiling, and wedges again on a counter that never moves.
   * Nothing retires it, and each lap's live lease tells the stalled arm the fleet is fine — which
   * is the very thing the ceiling exists to stop.
   *
   * <p>The sibling test above uses a client that succeeds on the second fetch, because its subject
   * is the worker coming back. This one uses a peer that is never coming back, because its subject
   * is the counter.
   */
  @Test
  public void aRequestThatWedgesEveryRunItGetsIsRetiredRatherThanLoopingForever() throws Exception {
    TestDb testDb = TestDb.create("liveness_ceiling_poison");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    PermanentlyWedgedChessClient client = new PermanentlyWedgedChessClient();
    IndexWorker worker = newWorker(client, dao, Duration.ofMillis(20));
    IndexWorkerLifecycle lifecycle =
        new IndexWorkerLifecycle(new InMemoryIndexQueue(), worker, dao, clock);
    Callable<Boolean> onePoll = lifecycle::claimAndRunOne;

    List<Integer> attemptsPerCycle = new ArrayList<>();
    for (int cycle = 1; cycle <= IndexingRequestStore.MAX_ATTEMPTS; cycle++) {
      Future<Boolean> run = workerExecutor.submit(onePoll);
      assertThat(client.awaitFetches(cycle)).as("cycle %s reaches the wedge", cycle).isTrue();
      attemptsPerCycle.add(dao.findById(requestId).orElseThrow().attempts());
      clock.advance(RetentionPolicy.MAX_RUN.plus(RetentionPolicy.LEASE));
      assertThat(run.get(30, TimeUnit.SECONDS)).as("cycle %s ran a request", cycle).isTrue();
    }

    assertThat(attemptsPerCycle)
        .as("every wedge must cost an attempt; a flat counter is a request that loops forever")
        .isEqualTo(List.of(1, 2, 3));
    assertThat(workerExecutor.submit(onePoll).get(15, TimeUnit.SECONDS))
        .as("and a request whose attempts are spent is not claimable again")
        .isFalse();

    dao.reclaimStale(STALE_AFTER, clock.instant());
    assertThat(dao.findById(requestId).orElseThrow().status())
        .as("so the poison arm can finally answer the user instead of the wedge repeating")
        .isEqualTo("FAILED");
  }

  /**
   * The month is not extracted when the interrupt lands during title resolution.
   *
   * <p>{@code resolveTitles} is the third span that blocks, and it absorbs an interrupt by design —
   * title enrichment must never fail indexing, so it marks itself degraded and returns a usable
   * map. Walking on from there is worse than it looks, and the reason is not obvious: the drain
   * loop below cannot catch the interrupt on our behalf, because a {@link Future} that has already
   * completed returns from {@code get()} without ever checking the flag. So every game is submitted
   * to the shared pool, and a month at {@code BATCH_SIZE} flushes a partial batch mid-loop, before
   * the pre-flush checkpoint is ever reached. That flush is fenced but not refused — the ceiling
   * stops the heartbeat, it does not expire the lease, so the run still owns the row for another
   * {@link RetentionPolicy#LEASE}.
   *
   * <p>Interrupted directly rather than through the ceiling, and with the heartbeat parked at an
   * hour, so what is under test is the checkpoint and not the clock.
   *
   * <p>Counted on <em>submission</em> rather than on extraction finishing, and the difference is
   * the whole test. An earlier version asserted that no game was extracted, and that passes with
   * the checkpoint deleted: the drain loop's first {@code get()} lands on a future that has not
   * completed yet, so it checks the flag, throws, and the rest are cancelled. Whether any
   * extraction completes is a race the run usually wins. What is not a race is the submitting —
   * without the checkpoint every game goes to the pool before anything can stop it.
   */
  @Test
  public void anInterruptedRunDoesNotSubmitTheMonthItWasResolvingTitlesFor() throws Exception {
    TestDb testDb = TestDb.create("liveness_interrupt_titles");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    CountDownLatch insideLookup = new CountDownLatch(1);
    WedgedProfileChessClient client = new WedgedProfileChessClient(insideLookup);
    CountingExecutor pool = new CountingExecutor(2);
    try {
      IndexWorker worker =
          new IndexWorker(
              client,
              new FeatureExtractor(
                  new PgnParser(), new GameReplayer(), List.of(new CheckDetector())),
              dao,
              new NoOpGameFeatureStore(),
              new RecordingPeriodStore(),
              pool,
              clock,
              Duration.ofHours(1));

      AtomicReference<Thread> runThread = new AtomicReference<>();
      Future<?> run =
          workerExecutor.submit(
              () -> {
                runThread.set(Thread.currentThread());
                worker.process(message(requestId));
              });
      assertThat(insideLookup.await(15, TimeUnit.SECONDS))
          .as("a profile lookup should be parked on the pool")
          .isTrue();
      // resolveTitles submits every lookup before it drains any of them, so by the time one is
      // running the title phase has finished submitting and this number has stopped moving.
      int submittedForTitles = pool.submissions();

      runThread.get().interrupt();
      run.get(30, TimeUnit.SECONDS);

      assertThat(pool.submissions())
          .as(
              "a run told to stop must not go on to hand the whole month to the pool, which is"
                  + " what flushes partial rows under a lease that has not lapsed yet")
          .isEqualTo(submittedForTitles);
    } finally {
      pool.shutdownNow();
    }
  }

  /**
   * An interrupted run must not leave a period behind claiming the month is done.
   *
   * <p>{@code indexed_periods} is keyed by (player, platform, month) and carries no request, so it
   * is the one write in a run that cannot be fenced — which makes ordering the only thing
   * protecting it. A run stopped part-way through a month has extracted some of its games and none
   * of the rest, and a period row stamped on that is not merely premature: the period cache reads
   * it as "indexed, {@code n} games", so every later request skips the month until retention sweeps
   * the row a week later.
   *
   * <p>Reached without the ceiling being involved, deliberately. Past the ceiling the fenced flush
   * fails first and the run unwinds before it can get here, so a ceiling-driven test would pass
   * with no guard at all. An interrupt arriving while the lease is perfectly live is the case that
   * needs the guard, and it is the ordinary one — anything may interrupt a worker thread.
   */
  @Test
  @Timeout(60)
  public void anInterruptedRunDoesNotStampAPeriodForAMonthItOnlyHalfIndexed() throws Exception {
    TestDb testDb = TestDb.create("liveness_interrupt_period");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    CountDownLatch insideExtraction = new CountDownLatch(1);
    CountDownLatch releaseExtraction = new CountDownLatch(1);
    RecordingPeriodStore periods = new RecordingPeriodStore();
    IndexWorker worker =
        newWorker(
            new OneGameChessClient(),
            dao,
            // No ceiling in play: the lease stays live for the whole test, so the flush that
            // precedes the period write would succeed and the run would carry on to write it.
            Duration.ofHours(1),
            new BlockingFeatureExtractor(insideExtraction, releaseExtraction),
            periods);

    AtomicReference<Thread> runThread = new AtomicReference<>();
    Future<?> run =
        workerExecutor.submit(
            () -> {
              runThread.set(Thread.currentThread());
              worker.process(message(requestId));
            });
    assertThat(insideExtraction.await(15, TimeUnit.SECONDS))
        .as("the run should be waiting on the extraction pool")
        .isTrue();

    runThread.get().interrupt();
    run.get(30, TimeUnit.SECONDS);
    releaseExtraction.countDown();

    assertThat(periods.upserts.get())
        .as(
            "a month whose games were only half extracted was recorded as indexed, so every later"
                + " request will skip it until retention sweeps the row")
        .isZero();
    assertThat(dao.findById(requestId).orElseThrow().status())
        .as("and being stopped is not a failure of the range")
        .isEqualTo("PROCESSING");
  }

  /**
   * A run told to stop must stop where it is, whatever the call it was in chose to report.
   *
   * <p>An interrupt does not always arrive as an exception. A great deal of blocking code — every
   * library this worker does not own included — catches {@link InterruptedException}, restores the
   * status, and returns normally. From the run's side that is indistinguishable from a month with
   * no games in it, and both of the things it then does are wrong: it writes an empty period, which
   * the period cache will honour for a week, and it moves on to the next month, which is the
   * opposite of stopping.
   *
   * <p>Nothing to do with the ceiling, deliberately. Past the ceiling the fenced writes refuse and
   * the run unwinds on its own, so a ceiling-driven version of this passes with no guard at all.
   * The case that needs one is an interrupt arriving while the lease is perfectly live.
   */
  @Test
  @Timeout(60)
  public void anInterruptedRunStopsAtTheMonthItWasOnEvenIfTheFetchSwallowedIt() throws Exception {
    TestDb testDb = TestDb.create("liveness_interrupt_months");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    CountDownLatch insideFetch = new CountDownLatch(1);
    SwallowingChessClient client = new SwallowingChessClient(insideFetch);
    RecordingPeriodStore periods = new RecordingPeriodStore();
    IndexWorker worker =
        newWorker(
            client,
            dao,
            Duration.ofHours(1),
            new FeatureExtractor(new PgnParser(), new GameReplayer(), List.of(new CheckDetector())),
            periods);

    AtomicReference<Thread> runThread = new AtomicReference<>();
    Future<?> run =
        workerExecutor.submit(
            () -> {
              runThread.set(Thread.currentThread());
              worker.process(message(requestId, "2024-01", "2024-03"));
            });
    assertThat(insideFetch.await(15, TimeUnit.SECONDS)).isTrue();

    runThread.get().interrupt();
    run.get(30, TimeUnit.SECONDS);

    assertThat(client.fetches.get())
        .as("the run carried on into the rest of the range after being told to stop")
        .isEqualTo(1);
    assertThat(periods.upserts.get())
        .as(
            "an interrupted fetch's empty answer was recorded as a month with no games in it, so"
                + " every later request will skip that month until retention sweeps the row")
        .isZero();
    assertThat(dao.findById(requestId).orElseThrow().status())
        .as("and being stopped is not a failure of the range")
        .isEqualTo("PROCESSING");
  }

  /**
   * The pool the run leaves behind, which the run's own interrupt does nothing about.
   *
   * <p>Interrupting the run thread frees the poller. It does not free the extraction pool threads
   * that run submitted work to, and those are the ones holding chess.com connections: title
   * resolution submits a profile lookup per distinct opponent, so the same silent peer that wedged
   * the archive fetch wedges every one of them. Unlike the run's own thread, that pool is shared by
   * every request on the instance — leaving threads parked in it recovers one poll loop while
   * quietly retiring the capacity behind it, which is the same failure arriving a size down.
   *
   * <p>#1282 named this when it named interrupting the run: the pool has to be interruptible too,
   * "which wants checking". This is the check.
   */
  @Test
  @Timeout(60)
  public void anInterruptedRunLetsGoOfTheLookupsItLeftOnTheExtractionPool() throws Exception {
    TestDb testDb = TestDb.create("liveness_interrupt_pool");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    CountDownLatch insideLookup = new CountDownLatch(1);
    WedgedProfileChessClient client = new WedgedProfileChessClient(insideLookup);
    IndexWorker worker =
        newWorker(
            client,
            dao,
            Duration.ofHours(1),
            new FeatureExtractor(new PgnParser(), new GameReplayer(), List.of(new CheckDetector())),
            new RecordingPeriodStore());

    AtomicReference<Thread> runThread = new AtomicReference<>();
    Future<?> run =
        workerExecutor.submit(
            () -> {
              runThread.set(Thread.currentThread());
              worker.process(message(requestId));
            });
    assertThat(insideLookup.await(15, TimeUnit.SECONDS))
        .as("a profile lookup should be parked on the pool")
        .isTrue();

    runThread.get().interrupt();
    run.get(30, TimeUnit.SECONDS);

    assertThat(client.lookupInterrupted.await(15, TimeUnit.SECONDS))
        .as(
            "the profile lookup is still parked on a chess.com connection that will never answer,"
                + " on a pool thread every other request on this instance has to share")
        .isTrue();
  }

  /**
   * The control. Same fixture, same single game, nothing interrupting it — and the period is
   * written. Without this the period tests above pass just as well against a worker that never
   * records a period at all, or against a fixture whose extraction never returns anything to
   * record.
   */
  @Test
  @Timeout(60)
  public void anUninterruptedRunOverTheSameMonthDoesStampItsPeriod() throws Exception {
    TestDb testDb = TestDb.create("liveness_interrupt_period_control");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    CountDownLatch insideExtraction = new CountDownLatch(1);
    CountDownLatch releaseExtraction = new CountDownLatch(1);
    releaseExtraction.countDown();
    RecordingPeriodStore periods = new RecordingPeriodStore();
    IndexWorker worker =
        newWorker(
            new OneGameChessClient(),
            dao,
            Duration.ofHours(1),
            new BlockingFeatureExtractor(insideExtraction, releaseExtraction),
            periods);

    workerExecutor.submit(() -> worker.process(message(requestId))).get(30, TimeUnit.SECONDS);

    assertThat(periods.upserts.get()).isEqualTo(1);
    assertThat(dao.findById(requestId).orElseThrow().status()).isEqualTo("COMPLETED");
  }

  /**
   * The ordering that stops an interrupt outliving the run it was aimed at.
   *
   * <p>A beat can read the handle, be descheduled, and come back after the run has finished and the
   * poller has moved on to the next request. Delivering the interrupt then lands it on work that
   * has nothing to do with the wedge, and nobody left to consume it. The window is small and
   * entirely real, and it is asserted here rather than in a scheduled race — a test that had to win
   * a race would be a test that passes whenever it loses.
   */
  @Test
  public void aHandleThatHasFinishedDoesNotInterruptTheThreadItNamed() {
    IndexWorker.RunHandle handle = new IndexWorker.RunHandle(Thread.currentThread());

    assertThat(handle.finish()).as("the run ended without ever being interrupted").isFalse();
    handle.interruptRun();

    assertThat(Thread.interrupted())
        .as("a late beat interrupted a thread that had already gone back to polling")
        .isFalse();
  }

  /** And the converse: an interrupt delivered in time is delivered, and reported for consuming. */
  @Test
  public void aHandleInterruptedBeforeItFinishedSaysSo() {
    IndexWorker.RunHandle handle = new IndexWorker.RunHandle(Thread.currentThread());
    handle.interruptRun();

    assertThat(Thread.interrupted()).as("the run's thread really was interrupted").isTrue();
    assertThat(handle.finish())
        .as("and the run is told, so it knows the status is its own to clear")
        .isTrue();
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

  /**
   * A run that genuinely fails, under a lease that lapsed with nobody taking it, must still record
   * the failure.
   *
   * <p>The recovery path is not a nicety for the happy cases. Progress, the flush and COMPLETED all
   * renew-and-retry when their own lease has merely lapsed; the FAILED write went straight at the
   * store, so the one write that exists to explain a failure was the one most likely to be refused
   * — and refused for a reason that has nothing to do with the failure. What follows is worse than
   * a missing log line: the row stays PROCESSING holding its dedupe slot, so the range is blocked
   * until the hourly sweep retires it under a message about an owner that stopped responding, which
   * is not what happened. The user is told to wait, then told the wrong thing.
   *
   * <p>Reachable exactly when it hurts most: a database problem that throws out of the run is the
   * same database problem that stalls the heartbeat.
   */
  @Test
  @Timeout(30)
  public void aRunThatFailsUnderALapsedLeaseStillRecordsTheFailure() throws Exception {
    TestDb testDb = TestDb.create("liveness_failed_lapsed");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    CountDownLatch insideFetch = new CountDownLatch(1);
    CountDownLatch releaseFetch = new CountDownLatch(1);
    IndexWorker worker =
        newWorker(new ThrowingChessClient(insideFetch, releaseFetch), dao, Duration.ofHours(1));

    Future<?> run = workerExecutor.submit(() -> worker.process(message(requestId)));
    assertThat(insideFetch.await(15, TimeUnit.SECONDS)).isTrue();

    clock.advance(RetentionPolicy.LEASE.plusMinutes(1));
    assertThat(dao.holdsLease(requestId, worker.ownerId(), clock.instant()))
        .as("precondition: the lease really has lapsed, and nobody has taken it")
        .isFalse();

    releaseFetch.countDown();
    run.get(20, TimeUnit.SECONDS);

    IndexingRequestStore.IndexingRequest row = dao.findById(requestId).orElseThrow();
    assertThat(row.status())
        .as("the failure has to be recorded, not left for the sweep to mislabel")
        .isEqualTo("FAILED");
    assertThat(row.errorMessage()).contains("Indexing failed");
    assertThat(
            dao.createOrAdopt(
                    PLAYER, PLATFORM, MONTH, MONTH, false, false, STALE_AFTER, clock.instant())
                .created())
        .as("and the dedupe slot is released, so the range is requestable again immediately")
        .isTrue();
  }

  /**
   * One process, two ways in, one run.
   *
   * <p>#1279 gave this process a second entry point. The poller claims from the table; {@code
   * submitHybrid} hands a message straight to {@code process}. Both can reach the same row — the
   * poller can claim it in the gap between {@code createOrAdopt} committing and the inline call
   * starting — and the lease cannot separate them, because {@code ownerId} names the process and
   * {@code claim} therefore admits its own holder. That is correct for the question the lease
   * answers and useless for this one.
   *
   * <p>Without an in-process guard both runs pass every fence, so the whole range is fetched and
   * extracted twice, one terminal write is refused, and nothing anywhere says so.
   */
  @Test
  @Timeout(30)
  public void oneProcessRunsARequestOnceEvenWhenBothEntryPointsReachIt() throws Exception {
    TestDb testDb = TestDb.create("liveness_two_entries");
    IndexingRequestDao dao = new IndexingRequestDao(testDb.jdbi(), clock);
    UUID requestId = claim(dao).request().id();

    CountDownLatch insideFetch = new CountDownLatch(1);
    CountDownLatch releaseFetch = new CountDownLatch(1);
    CountingChessClient client = new CountingChessClient(insideFetch, releaseFetch);
    IndexWorker worker = newWorker(client, dao, Duration.ofMillis(20));

    // The poller's run gets there first and parks in the fetch.
    Future<?> viaPoller = workerExecutor.submit(() -> worker.process(message(requestId)));
    assertThat(insideFetch.await(15, TimeUnit.SECONDS)).isTrue();

    // The inline dispatch arrives for the same row, in the same process, on this thread.
    worker.process(message(requestId));

    releaseFetch.countDown();
    viaPoller.get(20, TimeUnit.SECONDS);

    assertThat(client.fetches.get())
        .as("the range was indexed twice — the same games fetched and extracted by both runs")
        .isEqualTo(1);
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
    return dao.createOrAdopt(
        PLAYER, PLATFORM, MONTH, MONTH, false, false, STALE_AFTER, clock.instant());
  }

  private IndexMessage message(UUID requestId) {
    return message(requestId, MONTH, MONTH);
  }

  private IndexMessage message(UUID requestId, String startMonth, String endMonth) {
    return new IndexMessage(requestId, PLAYER, PLATFORM, startMonth, endMonth, false);
  }

  /** A worker that parks inside the archive fetch until released. */
  private IndexWorker blockingWorker(
      IndexingRequestStore store, CountDownLatch entered, CountDownLatch release) {
    return newWorker(
        new ClockJumpingChessClient(clock, null, entered, release), store, Duration.ofMillis(20));
  }

  private IndexWorker newWorker(
      ChessClient client, IndexingRequestStore store, Duration heartbeatInterval) {
    return newWorker(
        client,
        store,
        heartbeatInterval,
        new FeatureExtractor(new PgnParser(), new GameReplayer(), List.of(new CheckDetector())),
        new NoOpPeriodStore());
  }

  private IndexWorker newWorker(
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
  private static final class InterruptIgnoringChessClient extends ChessClient {
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
  private static final class PermanentlyWedgedChessClient extends ChessClient {
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
  private static final class CountingExecutor extends ThreadPoolExecutor {
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
  private static final class CountingDao extends IndexingRequestDao {
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

  /** Parks in the archive fetch until released, and counts how many runs got that far. */
  private static final class CountingChessClient extends ChessClient {
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
  private static final class WedgedChessClient extends ChessClient {
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
  private static final class SwallowingChessClient extends ChessClient {
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
  private static final class OneGameChessClient extends ChessClient {
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
  private static final class WedgedProfileChessClient extends ChessClient {
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
  private static final class BlockingFeatureExtractor extends FeatureExtractor {
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
  private static final class RecordingPeriodStore implements IndexedPeriodStore {
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
  private static final class ThrowingChessClient extends ChessClient {
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
