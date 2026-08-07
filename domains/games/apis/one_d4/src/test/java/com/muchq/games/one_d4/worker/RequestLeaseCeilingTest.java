package com.muchq.games.one_d4.worker;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.db.IndexingRequestDao;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.db.RetentionPolicy;
import com.muchq.games.one_d4.db.TestDb;
import com.muchq.games.one_d4.queue.InMemoryIndexQueue;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.Callable;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

/**
 * The lease heartbeat and the run ceiling: a slow run keeps renewing and keeps vouching for the
 * fleet, a run past the ceiling lets go, is cut loose from the call it is stuck in, and a request
 * that wedges every run it gets is retired rather than looping forever. Fixtures and the liveness
 * story live in {@link RequestLivenessHarness}.
 */
public class RequestLeaseCeilingTest extends RequestLivenessHarness {

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

    // This run left through LeaseLostException — past the ceiling every fenced write refuses, so
    // that is the ordinary way out, and it is a branch that knows nothing about interrupts. The
    // two assertions above pass with the row still owned: an expired lease is not a released one.
    // What separates them is what happens when this same worker comes back for it.
    int attemptsBeforeReclaim = dao.findById(requestId).orElseThrow().attempts();
    assertThat(dao.claim(requestId, wedged.ownerId(), RetentionPolicy.LEASE, clock.instant()))
        .as("the row is claimable again")
        .isTrue();
    assertThat(dao.findById(requestId).orElseThrow().attempts())
        .as(
            "and this worker re-claiming its own wedge has to cost an attempt, which only happens"
                + " if the run let the row go on the way out")
        .isEqualTo(attemptsBeforeReclaim + 1);
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
}
