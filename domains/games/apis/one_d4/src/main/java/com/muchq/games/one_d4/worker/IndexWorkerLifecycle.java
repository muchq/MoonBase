package com.muchq.games.one_d4.worker;

import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.db.RetentionPolicy;
import com.muchq.games.one_d4.queue.IndexMessage;
import com.muchq.games.one_d4.queue.IndexQueue;
import io.micronaut.context.event.ApplicationEventListener;
import io.micronaut.runtime.server.event.ServerStartupEvent;
import java.time.Clock;
import java.time.Duration;
import java.util.Optional;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Pulls indexing work from {@code indexing_requests} and runs it.
 *
 * <p>The table is the queue. Before #1279 this loop drained an {@link IndexQueue} — a {@code
 * LinkedBlockingQueue} inside one JVM — so a request could only ever be processed by the process
 * that accepted the submit. Adding an instance added no throughput for work already queued; a
 * restart lost the messages while the rows survived; and the documented two-JVM deployment (REST
 * and MCP against one Postgres) sent load wherever the submit happened to land. Any worker can now
 * take any request.
 *
 * <p>The queue is still here, demoted to a wake-up nudge. Polling alone would add up to {@link
 * #IDLE_POLL} of latency to a freshly submitted request; the nudge removes that when the submit
 * arrived at this instance, and losing it costs latency rather than work, because the row is in the
 * table either way. Its payload is deliberately ignored — reading it would reintroduce the
 * assumption that the message and the worker share a process.
 *
 * <p>Claiming is what makes concurrent pollers safe. {@link IndexingRequestStore#claimNext} hands
 * back a request this worker now owns, and every write the run makes is fenced on that ownership
 * (#1278), so two instances polling one table cannot end up on the same range.
 */
public class IndexWorkerLifecycle implements ApplicationEventListener<ServerStartupEvent> {
  private static final Logger LOG = LoggerFactory.getLogger(IndexWorkerLifecycle.class);

  /**
   * How long an idle poller waits for a nudge before asking the table again. The fallback matters
   * on its own: work submitted to another instance produces no local nudge, so this bounds how long
   * a request sits unclaimed when the instance that accepted it cannot run it.
   */
  private static final Duration IDLE_POLL = Duration.ofSeconds(5);

  /** Backoff after a failed poll, so a database outage does not become a hot loop. */
  private static final Duration ERROR_BACKOFF = Duration.ofSeconds(5);

  private final IndexQueue queue;
  private final IndexWorker worker;
  private final IndexingRequestStore requestStore;
  private final Clock clock;
  private volatile boolean running = true;

  public IndexWorkerLifecycle(
      IndexQueue queue, IndexWorker worker, IndexingRequestStore requestStore, Clock clock) {
    this.queue = queue;
    this.worker = worker;
    this.requestStore = requestStore;
    this.clock = clock;
  }

  @Override
  public void onApplicationEvent(ServerStartupEvent event) {
    Thread pollerThread = Thread.ofVirtual().name("index-worker").unstarted(this::pollLoop);
    pollerThread.setDaemon(true);
    pollerThread.start();
    LOG.info("Index worker started, claiming as {}", worker.ownerId());
  }

  private void pollLoop() {
    while (running) {
      try {
        if (claimAndRunOne()) {
          // Straight round rather than waiting: a backlog should drain at the speed of the work,
          // not at the speed of the poll interval.
          continue;
        }
        // Idle. Block for a nudge from a local submit, or time out and ask the table again — the
        // timeout is what picks up work submitted to another instance.
        queue.poll(IDLE_POLL);
      } catch (Exception e) {
        LOG.error("Error in index worker poll loop", e);
        sleepQuietly(ERROR_BACKOFF);
      }
    }
  }

  /** Discards pending wake-ups. Their payloads are irrelevant; only the wake-up ever mattered. */
  private void drainNudges() {
    while (queue.poll(Duration.ZERO).isPresent()) {
      // Nothing to do with it. The table said what to run.
    }
  }

  /** Returns true if a request was claimed and run, false if there was nothing to take. */
  boolean claimAndRunOne() {
    Optional<IndexingRequestStore.IndexingRequest> claimed =
        requestStore.claimNext(worker.ownerId(), RetentionPolicy.LEASE, clock.instant());
    if (claimed.isEmpty()) {
      return false;
    }
    // process() re-claims, which is a no-op extension for the owner we already are — claim's
    // predicate admits its own holder. Going through the same entry point as the inline path keeps
    // one place where a run starts.
    worker.process(toMessage(claimed.get()));
    // Nudges are consumed on the working path too, not only when idle. This loop is the queue's
    // only consumer and it never reaches the blocking poll while the table has work, so on a busy
    // instance every accepted submit would leave a message behind forever — and the accumulated
    // stale nudges would then fire back-to-back the moment things went quiet, defeating IDLE_POLL
    // for that whole burst.
    drainNudges();
    return true;
  }

  /**
   * Rebuilds the message from the row. Every field a run needs is a column, which is the property
   * that makes this table dispatchable — {@code skip_cache} was the last one that was not, and a
   * worker claiming this row would otherwise honour a cache the submitter asked to bypass.
   */
  private static IndexMessage toMessage(IndexingRequestStore.IndexingRequest r) {
    return new IndexMessage(
        r.id(),
        r.player(),
        r.platform(),
        r.startMonth(),
        r.endMonth(),
        r.excludeBullet(),
        r.skipCache());
  }

  private static void sleepQuietly(Duration duration) {
    try {
      Thread.sleep(duration.toMillis());
    } catch (InterruptedException e) {
      Thread.currentThread().interrupt();
    }
  }

  /**
   * Stops taking work, and gives back whatever this process is holding.
   *
   * <p>Wired as the bean's {@code preDestroy} in both modules, which is the whole point — the
   * poller is a daemon thread, so without this a deploy is indistinguishable from a crash. The row
   * stays owned by a process that no longer exists, nothing may take it until the lease lapses five
   * minutes later, and the attempt spent claiming it is gone. A long-running request that outlives
   * three rolling restarts is then retired as poisoned, telling the user its range fails repeatedly
   * about a fleet that is working perfectly.
   *
   * <p>It hands back rather than draining, and that is deliberate. An indexing run has no bound — a
   * twelve-month range against a slow chess.com is legitimately long — so a shutdown that waits for
   * one is a shutdown that hangs, and the container kills it anyway. Handing back is what a drain
   * was for: the work moves to a surviving instance immediately, unspent.
   *
   * <p>The departing run is not interrupted, and the contrast with the {@link
   * RetentionPolicy#MAX_RUN} ceiling — which does interrupt — is the point rather than an
   * inconsistency. The ceiling interrupts to get a thread back, because the process is staying and
   * its poller has more work to claim. Here the process is leaving, so there is no slot worth
   * reclaiming, and the run needs no stopping: every write it makes is fenced on ownership it has
   * just given away, so the first one fails and the run unwinds itself. That is the same mechanism
   * that protects against a lapsed lease, reached a few minutes earlier.
   */
  /**
   * Whether this poller is still taking work. Exists so a test can boot a real context, close it,
   * and check the container actually invoked {@link #stop} — an annotation the tests only read back
   * off the factory method would prove nothing about whether Micronaut honours it.
   */
  public boolean isRunning() {
    return running;
  }

  public void stop() {
    running = false;
    for (UUID id : worker.inFlight()) {
      try {
        requestStore.handBack(id, worker.ownerId(), clock.instant());
      } catch (Exception e) {
        // Shutdown must not throw. Failing here costs the lease's five minutes and one attempt —
        // exactly the pre-#1279 behaviour — which is a worse outcome, not a broken one.
        LOG.warn("Could not hand request {} back on shutdown", id, e);
      }
    }
  }
}
