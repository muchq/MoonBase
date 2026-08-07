package com.muchq.games.one_d4.worker;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.db.IndexingRequestDao;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.db.RetentionPolicy;
import com.muchq.games.one_d4.db.TestDb;
import java.time.Duration;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

/**
 * Lease ownership under contention: renewals cannot revive a retired request, a worker whose lease
 * was taken stops writing, both entry points still run a request once, and neither retirement nor
 * the dedupe slot lets go of a request whose worker is mid-flight. Fixtures and the liveness story
 * live in {@link RequestLivenessHarness}.
 */
public class RequestLeaseOwnershipTest extends RequestLivenessHarness {

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
}
