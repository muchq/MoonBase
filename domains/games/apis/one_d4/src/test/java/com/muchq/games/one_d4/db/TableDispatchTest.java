package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.Callable;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

/**
 * The property #1279 exists for: any available worker can process any queued request.
 *
 * <p>Before this, a request could only ever run in the process that accepted the submit, because
 * the message went into that JVM's {@code InMemoryIndexQueue}. Adding an instance added no
 * throughput for work already queued, a restart lost the messages while the rows survived, and the
 * documented two-JVM deployment sent load wherever the submit happened to land. The table is the
 * queue now, and these tests are what says so.
 */
public class TableDispatchTest {

  private static final Duration LEASE = RetentionPolicy.LEASE;
  private static final Duration STALE_AFTER = RetentionPolicy.STALE_REQUEST;
  private static final Instant NOW = Instant.parse("2026-07-01T12:00:00Z");

  private TestDb testDb;
  private IndexingRequestDao dao;
  private ExecutorService pool;

  @BeforeEach
  public void setUp() {
    testDb = TestDb.create("table_dispatch");
    dao = new IndexingRequestDao(testDb.jdbi());
    pool = Executors.newFixedThreadPool(4);
  }

  @AfterEach
  public void tearDown() {
    pool.shutdownNow();
  }

  /** The headline, stated plainly. Submitted by one instance, run by another. */
  @Test
  public void aRequestSubmittedByOneInstanceIsClaimableByAnother() {
    UUID id = submit("handoff", "2026-01").request().id();

    Optional<IndexingRequestStore.IndexingRequest> taken =
        dao.claimNext("instance-b/1/bbbb", LEASE, NOW);

    assertThat(taken)
        .as("a second instance must be able to take work the first accepted")
        .hasValueSatisfying(r -> assertThat(r.id()).isEqualTo(id));
    assertThat(dao.holdsLease(id, "instance-b/1/bbbb", NOW)).isTrue();
  }

  /**
   * {@code skipCache} has to survive the table, because it stops being the submitter's business the
   * moment another process runs the request.
   *
   * <p>Every other field a run needs was already a column. This one lived only in the queue
   * message, so a worker claiming the row would have honoured the period cache for a request that
   * asked to bypass it — silently, and only for requests that crossed an instance boundary.
   */
  @Test
  public void skipCacheSurvivesTheHandoff() {
    dao.createOrAdopt("bypass", "CHESS_COM", "2026-02", "2026-02", false, true, STALE_AFTER, NOW);

    assertThat(dao.claimNext("instance-b/1/bbbb", LEASE, NOW))
        .hasValueSatisfying(r -> assertThat(r.skipCache()).isTrue());
  }

  /** And the default is not silently sticky the other way. */
  @Test
  public void aCacheRespectingRequestStaysCacheRespecting() {
    submit("normal", "2026-03");

    assertThat(dao.claimNext("instance-b/1/bbbb", LEASE, NOW))
        .hasValueSatisfying(r -> assertThat(r.skipCache()).isFalse());
  }

  /**
   * Oldest first: the queue it replaces was FIFO, and a poller that skips ahead starves the head.
   */
  @Test
  public void workIsHandedOutOldestFirst() {
    // Inserted newest-first on purpose. With the older row also inserted first, physical scan
    // order already matches the asserted order and deleting the ORDER BY changes nothing.
    UUID second = submitAt("fifo-b", "2026-05", NOW.plusSeconds(60)).request().id();
    UUID first = submitAt("fifo-a", "2026-04", NOW).request().id();

    assertThat(dao.claimNext("w1", LEASE, NOW.plusSeconds(120)))
        .hasValueSatisfying(r -> assertThat(r.id()).isEqualTo(first));
    assertThat(dao.claimNext("w2", LEASE, NOW.plusSeconds(120)))
        .hasValueSatisfying(r -> assertThat(r.id()).isEqualTo(second));
  }

  /** A request already held is not offered again while its lease is live. */
  @Test
  public void heldWorkIsNotHandedToASecondWorker() {
    submit("exclusive", "2026-06");
    assertThat(dao.claimNext("w1", LEASE, NOW)).isPresent();

    assertThat(dao.claimNext("w2", LEASE, NOW.plusSeconds(1)))
        .as("the only request in the table is already owned")
        .isEmpty();
  }

  /**
   * Four pollers, four requests, all racing. Every request goes to exactly one worker.
   *
   * <p>This is the property the whole design rests on, and it does not come from the selection
   * query — that is an unlocked read, and all four are entitled to return the same candidates. It
   * comes from {@code claim} being a conditional UPDATE, so at most one racer's WHERE can match a
   * given row and the losers move on to the next candidate.
   */
  @Test
  @Timeout(30)
  public void concurrentPollersNeverHandTheSameRequestToTwoWorkers() throws Exception {
    int workers = 4;
    for (int i = 0; i < workers; i++) {
      submitAt("racer" + i, "2026-07", NOW.plusSeconds(i));
    }

    CyclicBarrier start = new CyclicBarrier(workers);
    List<Callable<Optional<UUID>>> tasks =
        java.util.stream.IntStream.range(0, workers)
            .<Callable<Optional<UUID>>>mapToObj(
                i ->
                    () -> {
                      start.await(20, TimeUnit.SECONDS);
                      return dao.claimNext("racer-worker-" + i, LEASE, NOW.plusSeconds(100))
                          .map(IndexingRequestStore.IndexingRequest::id);
                    })
            .toList();

    List<UUID> claimed =
        pool.invokeAll(tasks, 25, TimeUnit.SECONDS).stream()
            .map(TableDispatchTest::get)
            .flatMap(Optional::stream)
            .toList();

    assertThat(claimed).as("every worker should have found work").hasSize(workers);
    assertThat(Set.copyOf(claimed))
        .as("two workers were handed the same request")
        .hasSize(claimed.size());
  }

  /**
   * A request that keeps killing its worker is eventually stopped rather than toured around the
   * fleet. Releasing is unbounded by construction, so without the counter this loop never ends.
   */
  @Test
  public void aRequestThatOutlivesItsAttemptsStopsBeingOffered() {
    UUID id = submit("poison", "2026-08").request().id();

    for (int i = 0; i < IndexingRequestDao.MAX_ATTEMPTS; i++) {
      Instant at = NOW.plus(LEASE.multipliedBy(i));
      assertThat(dao.claimNext("worker-" + i, LEASE, at))
          .as("attempt %s should still be offered", i)
          .hasValueSatisfying(r -> assertThat(r.id()).isEqualTo(id));
    }

    Instant afterEveryLease = NOW.plus(LEASE.multipliedBy(IndexingRequestDao.MAX_ATTEMPTS + 1));
    assertThat(dao.claimNext("worker-last", LEASE, afterEveryLease))
        .as("a request that has burned its attempts must stop being handed out")
        .isEmpty();

    dao.reclaimStale(STALE_AFTER, afterEveryLease);
    assertThat(dao.findById(id).orElseThrow().status())
        .as("and the user is told, rather than left waiting on work nobody will run")
        .isEqualTo("FAILED");
  }

  // --- helpers ---------------------------------------------------------------------------------

  private static <T> T get(Future<T> f) {
    try {
      return f.get();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private IndexingRequestStore.Claim submit(String player, String month) {
    return submitAt(player, month, NOW);
  }

  private IndexingRequestStore.Claim submitAt(String player, String month, Instant at) {
    IndexingRequestDao stamped =
        new IndexingRequestDao(testDb.jdbi(), java.time.Clock.fixed(at, java.time.ZoneOffset.UTC));
    return stamped.createOrAdopt(player, "CHESS_COM", month, month, false, false, STALE_AFTER, at);
  }
}
