package com.muchq.games.one_d4.worker;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.db.IndexingRequestDao;
import com.muchq.games.one_d4.db.RetentionPolicy;
import com.muchq.games.one_d4.db.TestDb;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.GameReplayer;
import com.muchq.games.one_d4.engine.PgnParser;
import com.muchq.games.one_d4.motifs.CheckDetector;
import java.time.Duration;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

/**
 * The interrupt checkpoints at month boundaries: a run told to stop must not hand the month to the
 * extraction pool, stamp its period, or roll into the next month — plus the interrupt handle's own
 * contract. Fixtures and the liveness story live in {@link RequestLivenessHarness}.
 */
public class RequestInterruptCheckpointTest extends RequestLivenessHarness {

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

    // One count per distinct username in the fixture game ("White" and "Black"). Awaiting a
    // single lookup raced the pool against the submit loop: the first lookup can be running —
    // and tripping the latch — before the second is submitted, so the capture below read a
    // count that was still moving. Awaiting both means the title phase has finished submitting.
    CountDownLatch insideLookup = new CountDownLatch(2);
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
          .as("both profile lookups should be parked on the pool")
          .isTrue();
      // Stable, not racing: every lookup the title phase will ever submit is now wedged on the
      // pool, so nothing can move this number again except the extraction submissions the
      // checkpoint under test exists to prevent.
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
}
