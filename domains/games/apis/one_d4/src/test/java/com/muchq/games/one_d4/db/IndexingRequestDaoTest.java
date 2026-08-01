package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.sql.Timestamp;
import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import javax.sql.DataSource;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class IndexingRequestDaoTest {

  private static final Duration STALE_AFTER = Duration.ofHours(1);

  private IndexingRequestDao dao;
  private DataSource dataSource;
  private Instant now;

  @BeforeEach
  public void setUp() {
    TestDb testDb = TestDb.create("index_req_test");
    dataSource = testDb.dataSource();
    dao = new IndexingRequestDao(testDb.jdbi());
    now = Instant.parse("2026-07-01T12:00:00Z");
  }

  private IndexingRequestStore.IndexingRequest create(
      String player, String start, String end, boolean excludeBullet) {
    return dao.createOrAdopt(player, "CHESS_COM", start, end, excludeBullet, STALE_AFTER, now)
        .request();
  }

  private Optional<IndexingRequestStore.IndexingRequest> find(
      String player, String start, String end, boolean excludeBullet) {
    return dao.findExistingRequest(
        player, "CHESS_COM", start, end, excludeBullet, STALE_AFTER, now);
  }

  @Test
  public void findExistingRequest_returnsEmptyWhenNoRequests() {
    assertThat(find("player1", "2024-01", "2024-01", false)).isEmpty();
  }

  @Test
  public void findExistingRequest_returnsPendingRequestWithMatchingParams() {
    UUID id = create("player1", "2024-01", "2024-03", false).id();
    Optional<IndexingRequestStore.IndexingRequest> result =
        find("player1", "2024-01", "2024-03", false);
    assertThat(result).isPresent();
    assertThat(result.get().id()).isEqualTo(id);
    assertThat(result.get().status()).isEqualTo("PENDING");
    assertThat(result.get().player()).isEqualTo("player1");
    assertThat(result.get().platform()).isEqualTo("CHESS_COM");
    assertThat(result.get().startMonth()).isEqualTo("2024-01");
    assertThat(result.get().endMonth()).isEqualTo("2024-03");
  }

  @Test
  public void findExistingRequest_returnsProcessingRequest() {
    UUID id = create("player2", "2024-02", "2024-02", false).id();
    dao.updateStatus(id, "PROCESSING", null, 5);
    Optional<IndexingRequestStore.IndexingRequest> result =
        find("player2", "2024-02", "2024-02", false);
    assertThat(result).isPresent();
    assertThat(result.get().id()).isEqualTo(id);
    assertThat(result.get().status()).isEqualTo("PROCESSING");
    assertThat(result.get().gamesIndexed()).isEqualTo(5);
  }

  @Test
  public void findExistingRequest_ignoresCompletedRequest() {
    UUID id = create("player3", "2024-03", "2024-03", false).id();
    dao.updateStatus(id, "COMPLETED", null, 10);
    assertThat(find("player3", "2024-03", "2024-03", false)).isEmpty();
  }

  @Test
  public void findExistingRequest_ignoresFailedRequest() {
    UUID id = create("player4", "2024-04", "2024-04", false).id();
    dao.updateStatus(id, "FAILED", "Network error", 0);
    assertThat(find("player4", "2024-04", "2024-04", false)).isEmpty();
  }

  @Test
  public void findExistingRequest_returnsEmptyWhenParamsDiffer() {
    create("player5", "2024-01", "2024-01", false);
    assertThat(find("player5", "2024-02", "2024-02", false)).isEmpty();
  }

  @Test
  public void findExistingRequest_distinguishesByExcludeBullet() {
    UUID id = create("player6", "2024-05", "2024-05", true).id();

    Optional<IndexingRequestStore.IndexingRequest> match =
        find("player6", "2024-05", "2024-05", true);
    assertThat(match).isPresent();
    assertThat(match.get().id()).isEqualTo(id);
    assertThat(match.get().excludeBullet()).isTrue();

    assertThat(find("player6", "2024-05", "2024-05", false)).isEmpty();
  }

  // --- #1249: the live slot is exclusive ------------------------------------------------------

  @Test
  public void createOrAdopt_secondIdenticalSubmitAdoptsTheFirstInsteadOfCreating() {
    IndexingRequestStore.Claim first =
        dao.createOrAdopt("same", "CHESS_COM", "2024-01", "2024-01", false, STALE_AFTER, now);
    IndexingRequestStore.Claim second =
        dao.createOrAdopt("same", "CHESS_COM", "2024-01", "2024-01", false, STALE_AFTER, now);

    assertThat(first.created()).isTrue();
    assertThat(second.created()).isFalse();
    assertThat(second.request().id()).isEqualTo(first.request().id());
    assertThat(countRequests()).isEqualTo(1);
  }

  /**
   * The whole point of the constraint: concurrent identical submits must produce exactly one unit
   * of work. Two of them double every motif count for the games they share, because the occurrence
   * flush deletes and re-inserts under fresh UUIDs in separate transactions.
   */
  @Test
  public void createOrAdopt_concurrentIdenticalSubmitsProduceExactlyOneRequest() throws Exception {
    int threads = 8;
    ExecutorService pool = Executors.newFixedThreadPool(threads);
    CountDownLatch startLine = new CountDownLatch(1);
    CountDownLatch done = new CountDownLatch(threads);
    AtomicInteger createdCount = new AtomicInteger();
    Set<UUID> claimedIds = ConcurrentHashMap.newKeySet();
    AtomicInteger failures = new AtomicInteger();

    for (int i = 0; i < threads; i++) {
      pool.submit(
          () -> {
            try {
              startLine.await();
              IndexingRequestStore.Claim claim =
                  dao.createOrAdopt(
                      "racer", "CHESS_COM", "2024-06", "2024-06", false, STALE_AFTER, now);
              claimedIds.add(claim.request().id());
              if (claim.created()) {
                createdCount.incrementAndGet();
              }
            } catch (Exception e) {
              failures.incrementAndGet();
            } finally {
              done.countDown();
            }
          });
    }

    startLine.countDown();
    assertThat(done.await(30, TimeUnit.SECONDS)).isTrue();
    pool.shutdownNow();

    assertThat(failures.get()).isEqualTo(0);
    assertThat(createdCount.get()).isEqualTo(1);
    assertThat(claimedIds).hasSize(1);
    assertThat(countRequests()).isEqualTo(1);
  }

  @Test
  public void createOrAdopt_terminalStatusFreesTheSlotForANewRequest() {
    IndexingRequestStore.Claim first =
        dao.createOrAdopt("done", "CHESS_COM", "2024-07", "2024-07", false, STALE_AFTER, now);
    dao.updateStatus(first.request().id(), "COMPLETED", null, 12);

    IndexingRequestStore.Claim second =
        dao.createOrAdopt("done", "CHESS_COM", "2024-07", "2024-07", false, STALE_AFTER, now);

    assertThat(second.created()).isTrue();
    assertThat(second.request().id()).isNotEqualTo(first.request().id());
    assertThat(countRequests()).isEqualTo(2);
  }

  @Test
  public void createOrAdopt_failedStatusAlsoFreesTheSlot() {
    IndexingRequestStore.Claim first =
        dao.createOrAdopt("retry", "CHESS_COM", "2024-08", "2024-08", false, STALE_AFTER, now);
    dao.updateStatus(first.request().id(), "FAILED", "boom", 0);

    IndexingRequestStore.Claim second =
        dao.createOrAdopt("retry", "CHESS_COM", "2024-08", "2024-08", false, STALE_AFTER, now);

    assertThat(second.created()).isTrue();
    assertThat(second.request().id()).isNotEqualTo(first.request().id());
  }

  /** PROCESSING is still in flight, so it must keep holding the slot. */
  @Test
  public void createOrAdopt_processingStatusKeepsHoldingTheSlot() {
    IndexingRequestStore.Claim first =
        dao.createOrAdopt("busy", "CHESS_COM", "2024-09", "2024-09", false, STALE_AFTER, now);
    dao.updateStatus(first.request().id(), "PROCESSING", null, 3);

    IndexingRequestStore.Claim second =
        dao.createOrAdopt("busy", "CHESS_COM", "2024-09", "2024-09", false, STALE_AFTER, now);

    assertThat(second.created()).isFalse();
    assertThat(second.request().id()).isEqualTo(first.request().id());
  }

  @Test
  public void createOrAdopt_differentTuplesDoNotCollide() {
    assertThat(create("a", "2024-01", "2024-01", false).id())
        .isNotEqualTo(create("b", "2024-01", "2024-01", false).id());
    assertThat(create("a", "2024-02", "2024-02", false).id()).isNotNull();
    assertThat(create("a", "2024-01", "2024-01", true).id()).isNotNull();
    assertThat(countRequests()).isEqualTo(4);
  }

  // --- #1250: a stranded holder must not lock the range out forever ---------------------------

  @Test
  public void createOrAdopt_reclaimsAStrandedHolderAndCreatesAReplacement() {
    IndexingRequestStore.Claim stranded =
        dao.createOrAdopt("ghost", "CHESS_COM", "2024-10", "2024-10", false, STALE_AFTER, now);
    backdateUpdatedAt(stranded.request().id(), now.minus(Duration.ofHours(3)));

    IndexingRequestStore.Claim replacement =
        dao.createOrAdopt("ghost", "CHESS_COM", "2024-10", "2024-10", false, STALE_AFTER, now);

    assertThat(replacement.created()).isTrue();
    assertThat(replacement.request().id()).isNotEqualTo(stranded.request().id());

    IndexingRequestStore.IndexingRequest retired =
        dao.findById(stranded.request().id()).orElseThrow();
    assertThat(retired.status()).isEqualTo("FAILED");
    assertThat(retired.errorMessage()).contains("Abandoned");
  }

  @Test
  public void createOrAdopt_doesNotReclaimAHolderThatIsStillFresh() {
    IndexingRequestStore.Claim live =
        dao.createOrAdopt("alive", "CHESS_COM", "2024-11", "2024-11", false, STALE_AFTER, now);
    backdateUpdatedAt(live.request().id(), now.minus(Duration.ofMinutes(30)));

    IndexingRequestStore.Claim second =
        dao.createOrAdopt("alive", "CHESS_COM", "2024-11", "2024-11", false, STALE_AFTER, now);

    assertThat(second.created()).isFalse();
    assertThat(second.request().id()).isEqualTo(live.request().id());
    assertThat(dao.findById(live.request().id()).orElseThrow().status()).isEqualTo("PENDING");
  }

  @Test
  public void findExistingRequest_ignoresAStrandedRow() {
    IndexingRequestStore.Claim stranded =
        dao.createOrAdopt("ghost2", "CHESS_COM", "2024-12", "2024-12", false, STALE_AFTER, now);
    backdateUpdatedAt(stranded.request().id(), now.minus(Duration.ofHours(5)));

    assertThat(find("ghost2", "2024-12", "2024-12", false)).isEmpty();
  }

  @Test
  public void reclaimStale_retiresStrandedRowsAndLeavesFreshOnes() {
    IndexingRequestStore.Claim old =
        dao.createOrAdopt("p1", "CHESS_COM", "2025-01", "2025-01", false, STALE_AFTER, now);
    IndexingRequestStore.Claim fresh =
        dao.createOrAdopt("p2", "CHESS_COM", "2025-01", "2025-01", false, STALE_AFTER, now);
    backdateUpdatedAt(old.request().id(), now.minus(Duration.ofHours(2)));

    assertThat(dao.reclaimStale(STALE_AFTER, now)).isEqualTo(1);

    assertThat(dao.findById(old.request().id()).orElseThrow().status()).isEqualTo("FAILED");
    assertThat(dao.findById(fresh.request().id()).orElseThrow().status()).isEqualTo("PENDING");
  }

  /**
   * A worker that marked its request PROCESSING and then died is the likelier strand than one that
   * never started, and the README promises both are handled. Narrowing {@code retire}'s predicate
   * to PENDING alone left every other reclamation test green, because they all strand a row that
   * never left PENDING.
   */
  @Test
  public void reclaimStale_retiresAStrandedProcessingRow() {
    IndexingRequestStore.Claim claim =
        dao.createOrAdopt("halfdone", "CHESS_COM", "2025-07", "2025-07", false, STALE_AFTER, now);
    dao.updateStatus(claim.request().id(), "PROCESSING", null, 4);
    backdateUpdatedAt(claim.request().id(), now.minus(Duration.ofHours(3)));

    assertThat(dao.reclaimStale(STALE_AFTER, now)).isEqualTo(1);

    IndexingRequestStore.IndexingRequest retired = dao.findById(claim.request().id()).orElseThrow();
    assertThat(retired.status()).isEqualTo("FAILED");
    assertThat(retired.gamesIndexed()).as("progress so far is preserved").isEqualTo(4);

    IndexingRequestStore.Claim replacement =
        dao.createOrAdopt("halfdone", "CHESS_COM", "2025-07", "2025-07", false, STALE_AFTER, now);
    assertThat(replacement.created()).isTrue();
  }

  @Test
  public void createOrAdopt_reclaimsAStrandedProcessingHolder() {
    IndexingRequestStore.Claim stranded =
        dao.createOrAdopt("halfdone2", "CHESS_COM", "2025-08", "2025-08", false, STALE_AFTER, now);
    dao.updateStatus(stranded.request().id(), "PROCESSING", null, 2);
    backdateUpdatedAt(stranded.request().id(), now.minus(Duration.ofHours(3)));

    IndexingRequestStore.Claim replacement =
        dao.createOrAdopt("halfdone2", "CHESS_COM", "2025-08", "2025-08", false, STALE_AFTER, now);

    assertThat(replacement.created()).isTrue();
    assertThat(replacement.request().id()).isNotEqualTo(stranded.request().id());
  }

  /**
   * The unfenced write path, which is still reachable: {@code IndexRequestService}'s
   * inline-dispatch failure handler has no lease to present, and neither do the tests below. A
   * status write with no token behind it must not resurrect a row that was already retired — that
   * would produce a live request holding no dedupe slot, and since the replacement already holds
   * the key the constraint could not see the second live row.
   *
   * <p>A worker cannot reach this state at all now: its writes go through {@code updateStatusOwned}
   * and are refused outright. This guard covers everything that is not a worker.
   */
  @Test
  public void updateStatus_cannotResurrectARetiredRequest() {
    IndexingRequestStore.Claim stranded =
        dao.createOrAdopt("zombie", "CHESS_COM", "2025-09", "2025-09", false, STALE_AFTER, now);
    UUID strandedId = stranded.request().id();
    backdateUpdatedAt(strandedId, now.minus(Duration.ofHours(3)));
    dao.reclaimStale(STALE_AFTER, now);

    IndexingRequestStore.Claim replacement =
        dao.createOrAdopt("zombie", "CHESS_COM", "2025-09", "2025-09", false, STALE_AFTER, now);
    assertThat(replacement.created()).isTrue();

    // An unfenced status write landing after the fact.
    dao.updateStatus(strandedId, "PROCESSING", null, 9);

    assertThat(dao.findById(strandedId).orElseThrow().status())
        .as("a retired request stays retired")
        .isEqualTo("FAILED");
    assertThat(countLiveRowsFor("zombie"))
        .as("exactly one live row per tuple, always")
        .isEqualTo(1);
  }

  /** A terminal write is still allowed to land on a retired row — it does not un-retire it. */
  @Test
  public void updateStatus_terminalWriteOnARetiredRequestStillRecordsTheOutcome() {
    IndexingRequestStore.Claim stranded =
        dao.createOrAdopt("late", "CHESS_COM", "2025-10", "2025-10", false, STALE_AFTER, now);
    UUID strandedId = stranded.request().id();
    backdateUpdatedAt(strandedId, now.minus(Duration.ofHours(3)));
    dao.reclaimStale(STALE_AFTER, now);

    dao.updateStatus(strandedId, "COMPLETED", null, 11);

    IndexingRequestStore.IndexingRequest row = dao.findById(strandedId).orElseThrow();
    assertThat(row.status()).isEqualTo("COMPLETED");
    assertThat(row.gamesIndexed()).isEqualTo(11);
  }

  /**
   * Every other test reaches the constraint only through {@code findByDedupeKey}, which short
   * circuits before the insert. This drives the constraint itself, so removing it from the
   * migration fails deterministically rather than only under a thread interleaving.
   */
  @Test
  public void schema_rejectsASecondRowWithTheSameDedupeKey() {
    create("constrained", "2025-11", "2025-11", false);
    String key =
        IndexingRequestDao.dedupeKey("constrained", "CHESS_COM", "2025-11", "2025-11", false);

    assertThatThrownBy(() -> insertRawWithDedupeKey(key))
        .as("the database, not the application, is what makes the slot exclusive")
        .isInstanceOf(Exception.class);
  }

  @Test
  public void schema_allowsManyTerminalRowsForOneTuple() {
    for (int i = 0; i < 3; i++) {
      IndexingRequestStore.Claim claim =
          dao.createOrAdopt("recycled", "CHESS_COM", "2025-12", "2025-12", false, STALE_AFTER, now);
      dao.updateStatus(claim.request().id(), "COMPLETED", null, i);
    }
    assertThat(countRequests()).isEqualTo(3);
  }

  @Test
  public void reclaimStale_leavesTerminalRowsAlone() {
    IndexingRequestStore.Claim completed =
        dao.createOrAdopt("p3", "CHESS_COM", "2025-02", "2025-02", false, STALE_AFTER, now);
    dao.updateStatus(completed.request().id(), "COMPLETED", null, 7);
    backdateUpdatedAt(completed.request().id(), now.minus(Duration.ofDays(2)));

    assertThat(dao.reclaimStale(STALE_AFTER, now)).isEqualTo(0);

    IndexingRequestStore.IndexingRequest row = dao.findById(completed.request().id()).orElseThrow();
    assertThat(row.status()).isEqualTo("COMPLETED");
    assertThat(row.gamesIndexed()).isEqualTo(7);
  }

  /** Reclaiming has to release the slot, not just relabel the row. */
  @Test
  public void reclaimStale_freesTheSlotSoTheRangeCanBeRequestedAgain() {
    IndexingRequestStore.Claim stranded =
        dao.createOrAdopt("p4", "CHESS_COM", "2025-03", "2025-03", false, STALE_AFTER, now);
    backdateUpdatedAt(stranded.request().id(), now.minus(Duration.ofHours(4)));

    dao.reclaimStale(STALE_AFTER, now);

    IndexingRequestStore.Claim replacement =
        dao.createOrAdopt("p4", "CHESS_COM", "2025-03", "2025-03", false, STALE_AFTER, now);
    assertThat(replacement.created()).isTrue();
  }

  // --- #1266: the table is bounded -------------------------------------------------------------

  @Test
  public void deleteOlderThan_removesRequestsPastTheThreshold() {
    IndexingRequestStore.Claim old =
        dao.createOrAdopt("old", "CHESS_COM", "2025-04", "2025-04", false, STALE_AFTER, now);
    IndexingRequestStore.Claim recent =
        dao.createOrAdopt("recent", "CHESS_COM", "2025-05", "2025-05", false, STALE_AFTER, now);
    dao.updateStatus(old.request().id(), "COMPLETED", null, 5);
    dao.updateStatus(recent.request().id(), "COMPLETED", null, 5);
    backdateCreatedAt(old.request().id(), now.minus(Duration.ofDays(40)));

    assertThat(dao.deleteOlderThan(now.minus(Duration.ofDays(30)))).isEqualTo(1);

    assertThat(dao.findById(old.request().id())).isEmpty();
    assertThat(dao.findById(recent.request().id())).isPresent();
  }

  /**
   * Age alone is not licence to delete. A request only reaches this age while still live if the
   * staleness reclamation never ran, and deleting it would take the row out from under a worker
   * that is still writing against its id — losing the FK's protection and the user's status.
   * Unreachable through {@code RetentionWorker}, which reclaims first, but the guard belongs on the
   * delete rather than in the caller's ordering.
   */
  @Test
  public void deleteOlderThan_refusesToSweepALiveRequestHoweverOldItIs() {
    IndexingRequestStore.Claim pending =
        dao.createOrAdopt("ancient", "CHESS_COM", "2025-04", "2025-04", false, STALE_AFTER, now);
    backdateCreatedAt(pending.request().id(), now.minus(Duration.ofDays(400)));

    assertThat(dao.deleteOlderThan(now.minus(Duration.ofDays(30)))).isEqualTo(0);
    assertThat(dao.findById(pending.request().id())).isPresent();
  }

  @Test
  public void deleteOlderThan_leavesRequestsThatStillOwnGames() {
    IndexingRequestStore.Claim owner =
        dao.createOrAdopt("owner", "CHESS_COM", "2025-06", "2025-06", false, STALE_AFTER, now);
    backdateCreatedAt(owner.request().id(), now.minus(Duration.ofDays(40)));
    insertGame(owner.request().id(), "https://chess.com/still-here");

    assertThat(dao.deleteOlderThan(now.minus(Duration.ofDays(30)))).isEqualTo(0);
    assertThat(dao.findById(owner.request().id())).isPresent();
  }

  // --- unchanged surface -----------------------------------------------------------------------

  @Test
  public void listRecent_returnsEmptyWhenNoRequests() {
    assertThat(dao.listRecent(10)).isEmpty();
  }

  @Test
  public void listRecent_respectsLimit() {
    create("playerA", "2024-01", "2024-01", false);
    create("playerB", "2024-02", "2024-02", false);
    create("playerC", "2024-03", "2024-03", false);

    List<IndexingRequestStore.IndexingRequest> results = dao.listRecent(2);

    assertThat(results).hasSize(2);
  }

  // --- helpers ---------------------------------------------------------------------------------

  // --- ownership leases (#1278) -----------------------------------------------------------------

  private static final Duration LEASE = Duration.ofMinutes(5);
  private static final String WORKER_A = "host-a/1/aaaa";
  private static final String WORKER_B = "host-b/2/bbbb";

  @Test
  public void claim_takesAnUnclaimedLiveRequest() {
    UUID id = create("lease1", "2025-01", "2025-01", false).id();

    assertThat(dao.claim(id, WORKER_A, LEASE, now)).isTrue();
    assertThat(dao.holdsLease(id, WORKER_A, now)).isTrue();
    assertThat(dao.holdsLease(id, WORKER_B, now)).isFalse();
  }

  @Test
  public void claim_isRefusedWhileAnotherOwnersLeaseIsLive() {
    UUID id = create("lease2", "2025-02", "2025-02", false).id();
    assertThat(dao.claim(id, WORKER_A, LEASE, now)).isTrue();

    assertThat(dao.claim(id, WORKER_B, LEASE, now.plus(Duration.ofMinutes(4))))
        .as("a live lease is not available to take")
        .isFalse();
    assertThat(dao.holdsLease(id, WORKER_A, now.plus(Duration.ofMinutes(4)))).isTrue();
  }

  @Test
  public void claim_takesOverOnceTheOtherOwnersLeaseHasExpired() {
    UUID id = create("lease3", "2025-03", "2025-03", false).id();
    dao.claim(id, WORKER_A, LEASE, now);

    Instant afterExpiry = now.plus(LEASE).plusSeconds(1);
    assertThat(dao.claim(id, WORKER_B, LEASE, afterExpiry)).isTrue();
    assertThat(dao.holdsLease(id, WORKER_A, afterExpiry))
        .as("the old owner is out the moment the new one is in")
        .isFalse();
  }

  /**
   * The expiry boundary, pinned because two predicates have to agree on it and nothing else makes
   * them. At the exact instant a lease expires it is no longer held ({@code lease_expires_at > now}
   * is false), so it must also be available to take. A strict {@code <} in the claim predicate
   * would leave one instant in which nobody holds the lease and nobody may take it.
   */
  @Test
  public void claim_takesOverAtTheExactInstantTheLeaseExpires() {
    UUID id = create("boundary", "2025-13", "2025-13", false).id();
    dao.claim(id, WORKER_A, LEASE, now);

    Instant atExpiry = now.plus(LEASE);
    assertThat(dao.holdsLease(id, WORKER_A, atExpiry))
        .as("the lease is over at its expiry, not one instant later")
        .isFalse();
    assertThat(dao.claim(id, WORKER_B, LEASE, atExpiry))
        .as("and what nobody holds must be available to take")
        .isTrue();
  }

  /** The same boundary on the sweep's side, so the two predicates cannot drift apart. */
  @Test
  public void reclaimStale_retiresALeaseAtTheExactInstantItExpires() {
    IndexingRequestStore.Claim claim =
        dao.createOrAdopt("boundary2", "CHESS_COM", "2025-14", "2025-14", false, STALE_AFTER, now);
    UUID id = claim.request().id();
    dao.claim(id, WORKER_A, LEASE, now);

    assertThat(dao.reclaimStale(STALE_AFTER, now.plus(LEASE))).isEqualTo(1);
    assertThat(dao.findById(id).orElseThrow().status()).isEqualTo("FAILED");
  }

  /**
   * Re-claiming as the same owner extends rather than fails. A worker restarted mid-request with a
   * stable id, or one whose claim call is retried after a timeout, must not lock itself out.
   */
  @Test
  public void claim_isIdempotentForTheSameOwner() {
    UUID id = create("lease4", "2025-04", "2025-04", false).id();
    dao.claim(id, WORKER_A, LEASE, now);

    Instant later = now.plus(Duration.ofMinutes(4));
    assertThat(dao.claim(id, WORKER_A, LEASE, later)).isTrue();
    assertThat(dao.holdsLease(id, WORKER_A, later.plus(Duration.ofMinutes(4))))
        .as("the second claim extended the lease rather than leaving the first one's expiry")
        .isTrue();
  }

  @Test
  public void claim_isRefusedOnceTheRequestIsTerminal() {
    UUID id = create("lease5", "2025-05", "2025-05", false).id();
    dao.updateStatus(id, "COMPLETED", null, 3);

    assertThat(dao.claim(id, WORKER_A, LEASE, now))
        .as("finished work is not there to be picked up")
        .isFalse();
  }

  @Test
  public void renewLease_failsOnceAnotherOwnerHasTakenTheRequest() {
    UUID id = create("lease6", "2025-06", "2025-06", false).id();
    dao.claim(id, WORKER_A, LEASE, now);

    Instant afterExpiry = now.plus(LEASE).plusSeconds(1);
    dao.claim(id, WORKER_B, LEASE, afterExpiry);

    assertThat(dao.renewLease(id, WORKER_A, LEASE, afterExpiry))
        .as("a worker that lost its lease must stop, not reassert itself over the replacement")
        .isFalse();
    assertThat(dao.holdsLease(id, WORKER_B, afterExpiry)).isTrue();
  }

  /**
   * Renewal is deliberately lenient about expiry: while {@code owner_id} still names this worker,
   * nobody else has taken the request, so a lapse caused by a paused GC or a database blip is
   * recoverable. Only a change of owner ends a claim.
   */
  @Test
  public void renewLease_recoversFromALapseNobodyElseTook() {
    UUID id = create("lease7", "2025-07", "2025-07", false).id();
    dao.claim(id, WORKER_A, LEASE, now);

    Instant afterExpiry = now.plus(LEASE).plusSeconds(1);
    assertThat(dao.holdsLease(id, WORKER_A, afterExpiry)).isFalse();
    assertThat(dao.renewLease(id, WORKER_A, LEASE, afterExpiry)).isTrue();
    assertThat(dao.holdsLease(id, WORKER_A, afterExpiry)).isTrue();
  }

  /**
   * The lease arm of reclamation: a crashed owner is reclaimable in minutes, without waiting out
   * the hour that governs work nobody ever picked up. That is the whole point of asking "is the
   * owner there?" instead of "has anything happened lately?".
   */
  @Test
  public void reclaimStale_takesAnExpiredLeaseWithoutWaitingForTheStalenessCutoff() {
    IndexingRequestStore.Claim claim =
        dao.createOrAdopt("crashed", "CHESS_COM", "2025-11", "2025-11", false, STALE_AFTER, now);
    UUID id = claim.request().id();
    dao.claim(id, WORKER_A, LEASE, now);

    Instant sixMinutesLater = now.plus(Duration.ofMinutes(6));
    assertThat(dao.reclaimStale(STALE_AFTER, sixMinutesLater))
        .as("six minutes is nowhere near the one-hour cutoff, but the lease is gone")
        .isEqualTo(1);
    assertThat(dao.findById(id).orElseThrow().status()).isEqualTo("FAILED");

    IndexingRequestStore.Claim replacement =
        dao.createOrAdopt(
            "crashed", "CHESS_COM", "2025-11", "2025-11", false, STALE_AFTER, sixMinutesLater);
    assertThat(replacement.created()).as("and the range is free again").isTrue();
  }

  /**
   * The other arm, and the reason they are separate. A request whose owner is renewing must not be
   * retired for being quiet, however long it has been quiet: renewal is a claim about the owner,
   * not about progress, and an indexing run can legitimately produce nothing for hours.
   *
   * <p>Collapsing the two — judging a claimed row by {@code updated_at} — is what made a slow
   * request indistinguishable from a dead one.
   */
  @Test
  public void reclaimStale_leavesAClaimedRequestAloneEvenWhenItHasBeenQuietForHours() {
    IndexingRequestStore.Claim claim =
        dao.createOrAdopt("patient", "CHESS_COM", "2025-12", "2025-12", false, STALE_AFTER, now);
    UUID id = claim.request().id();
    dao.claim(id, WORKER_A, LEASE, now);
    backdateUpdatedAt(id, now.minus(Duration.ofHours(3)));

    assertThat(dao.reclaimStale(STALE_AFTER, now))
        .as("the hour is for work nobody owns; this one has an owner and a live lease")
        .isZero();
    assertThat(dao.findById(id).orElseThrow().status()).isIn("PENDING", "PROCESSING");
  }

  @Test
  public void updateStatusOwned_isRefusedForAWorkerThatLostTheLease() {
    UUID id = create("fenced1", "2026-01", "2026-01", false).id();
    dao.claim(id, WORKER_A, LEASE, now);
    Instant afterExpiry = now.plus(LEASE).plusSeconds(1);
    dao.claim(id, WORKER_B, LEASE, afterExpiry);

    assertThat(dao.updateStatusOwned(id, WORKER_A, "COMPLETED", null, 99, afterExpiry)).isFalse();

    IndexingRequestStore.IndexingRequest row = dao.findById(id).orElseThrow();
    assertThat(row.status()).as("the replacement's row is untouched").isEqualTo("PENDING");
    assertThat(row.gamesIndexed()).isZero();
    assertThat(dao.holdsLease(id, WORKER_B, afterExpiry))
        .as("and the replacement still holds the lease the refused write would have cleared")
        .isTrue();
  }

  /**
   * An expired lease is refused too, even with {@code owner_id} unchanged — and asserted
   * <em>at</em> the expiry instant rather than a second past it.
   *
   * <p>Five predicates have to agree on this boundary, not two. {@code claim} and the reclamation
   * sweep hand the row over at {@code lease_expires_at <= now}; {@code holdsLease}, this, and the
   * flush's ownership probe authorize at {@code > now}. A second either side of the boundary tests
   * nothing about the boundary: relax any of the write-authorizing three to {@code >=} and there is
   * one instant where the replacement owns the row and the old owner may still write to it, which
   * is the state this whole change exists to prevent.
   */
  @Test
  public void updateStatusOwned_isRefusedAtTheExactInstantTheLeaseExpires() {
    UUID id = create("fenced2", "2026-02", "2026-02", false).id();
    dao.claim(id, WORKER_A, LEASE, now);

    assertThat(dao.updateStatusOwned(id, WORKER_A, "PROCESSING", null, 5, now.plus(LEASE)))
        .isFalse();
  }

  /**
   * Dedupe must answer "is this alive?" the same way reclamation does. This read runs first on
   * every submit and short-circuits, so a laxer predicate here silently overrides the stricter
   * reclamation behind it.
   *
   * <p>The case that exposed it: a worker claims, then is killed. Its lease lapses in minutes and
   * the row is reclaimable — but {@code updated_at} was bumped by its last renewal, so a predicate
   * that asks only about age keeps handing the dead request back to callers for the full hour. The
   * README's "five minutes" was delivered nowhere a user could reach.
   */
  @Test
  public void findExistingRequest_doesNotAnswerWithARequestWhoseLeaseHasLapsed() {
    dao.createOrAdopt("killed", "CHESS_COM", "2026-05", "2026-05", false, STALE_AFTER, now);
    UUID id = find("killed", "2026-05", "2026-05", false).orElseThrow().id();
    dao.claim(id, WORKER_A, LEASE, now);

    Instant afterLapse = now.plus(LEASE).plusSeconds(1);
    assertThat(
            dao.findExistingRequest(
                "killed", "CHESS_COM", "2026-05", "2026-05", false, STALE_AFTER, afterLapse))
        .as("a request whose owner is gone must not keep answering for the range")
        .isEmpty();
    // The boundary itself. reclaimStale takes the row at exactly this instant, so dedupe must
    // already have stopped offering it — otherwise there is a moment where the row is both
    // reclaimable and reported as the live holder.
    assertThat(
            dao.findExistingRequest(
                "killed", "CHESS_COM", "2026-05", "2026-05", false, STALE_AFTER, now.plus(LEASE)))
        .isEmpty();
  }

  /** The converse: a claimed request with a live lease is still the answer, however quiet it is. */
  @Test
  public void findExistingRequest_stillAnswersWithAClaimedRequestThatIsRenewing() {
    dao.createOrAdopt("busy", "CHESS_COM", "2026-06", "2026-06", false, STALE_AFTER, now);
    UUID id = find("busy", "2026-06", "2026-06", false).orElseThrow().id();
    dao.claim(id, WORKER_A, LEASE, now);
    backdateUpdatedAt(id, now.minus(Duration.ofHours(3)));
    dao.renewLease(id, WORKER_A, LEASE, now);

    assertThat(
            dao.findExistingRequest(
                "busy", "CHESS_COM", "2026-06", "2026-06", false, STALE_AFTER, now))
        .as("an owner that is renewing owns the range no matter how long the work takes")
        .isPresent();
  }

  /**
   * The two arms report different things to the user. {@code error_message} is surfaced by the API
   * and rendered in the web app, so telling someone whose worker crashed mid-run that nobody ever
   * picked their request up sends them looking for a dispatch problem that is not there.
   */
  @Test
  public void reclaimStale_saysWhichArmRetiredTheRequest() {
    IndexingRequestStore.Claim claimed =
        dao.createOrAdopt("crashed2", "CHESS_COM", "2026-07", "2026-07", false, STALE_AFTER, now);
    dao.claim(claimed.request().id(), WORKER_A, LEASE, now);

    IndexingRequestStore.Claim neverClaimed =
        dao.createOrAdopt("orphan", "CHESS_COM", "2026-08", "2026-08", false, STALE_AFTER, now);
    backdateUpdatedAt(neverClaimed.request().id(), now.minus(Duration.ofHours(3)));

    dao.reclaimStale(STALE_AFTER, now.plus(Duration.ofMinutes(6)));

    assertThat(dao.findById(claimed.request().id()).orElseThrow().errorMessage())
        .contains("stopped responding")
        .doesNotContain("no worker took ownership");
    assertThat(dao.findById(neverClaimed.request().id()).orElseThrow().errorMessage())
        .contains("no worker took ownership");
  }

  @Test
  public void updateStatusOwned_terminalWriteReleasesTheLeaseAndTheDedupeSlot() {
    IndexingRequestStore.Claim claim =
        dao.createOrAdopt("released", "CHESS_COM", "2026-03", "2026-03", false, STALE_AFTER, now);
    UUID id = claim.request().id();
    dao.claim(id, WORKER_A, LEASE, now);

    assertThat(dao.updateStatusOwned(id, WORKER_A, "COMPLETED", null, 12, now)).isTrue();

    IndexingRequestStore.IndexingRequest row = dao.findById(id).orElseThrow();
    assertThat(row.status()).isEqualTo("COMPLETED");
    assertThat(row.gamesIndexed()).isEqualTo(12);
    // Read straight off the row. holdsLease would answer false for a COMPLETED request whatever
    // the columns say, so it cannot tell a released lease from a stale one left behind.
    assertThat(columnOf(id, "owner_id")).as("the lease is released, not just unusable").isNull();
    assertThat(columnOf(id, "lease_expires_at")).isNull();
    assertThat(dao.holdsLease(id, WORKER_A, now)).isFalse();
    assertThat(
            dao.createOrAdopt(
                    "released", "CHESS_COM", "2026-03", "2026-03", false, STALE_AFTER, now)
                .created())
        .as("the range is requestable again the moment the work stops being in flight")
        .isTrue();
  }

  @Test
  public void updateStatusOwned_recordsProgressForTheHolder() {
    UUID id = create("holder", "2026-04", "2026-04", false).id();
    dao.claim(id, WORKER_A, LEASE, now);

    assertThat(dao.updateStatusOwned(id, WORKER_A, "PROCESSING", null, 7, now)).isTrue();

    IndexingRequestStore.IndexingRequest row = dao.findById(id).orElseThrow();
    assertThat(row.status()).isEqualTo("PROCESSING");
    assertThat(row.gamesIndexed()).isEqualTo(7);
    assertThat(dao.holdsLease(id, WORKER_A, now))
        .as("a progress write must not release the lease it was made under")
        .isTrue();
  }

  /** Reads a column the {@code IndexingRequest} record does not carry. */
  private Object columnOf(UUID id, String column) {
    try (var conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement("SELECT " + column + " FROM indexing_requests WHERE id = ?")) {
      ps.setObject(1, id);
      try (var rs = ps.executeQuery()) {
        rs.next();
        return rs.getObject(1);
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private void backdateUpdatedAt(UUID id, Instant updatedAt) {
    execute("UPDATE indexing_requests SET updated_at = ? WHERE id = ?", updatedAt, id);
  }

  private void backdateCreatedAt(UUID id, Instant createdAt) {
    execute("UPDATE indexing_requests SET created_at = ? WHERE id = ?", createdAt, id);
  }

  private void execute(String sql, Instant timestamp, UUID id) {
    try (var conn = dataSource.getConnection();
        var ps = conn.prepareStatement(sql)) {
      ps.setTimestamp(1, Timestamp.from(timestamp));
      ps.setObject(2, id);
      ps.executeUpdate();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private void insertGame(UUID requestId, String gameUrl) {
    try (var conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement(
                "INSERT INTO game_features (id, request_id, game_url, platform) VALUES (?, ?, ?,"
                    + " 'CHESS_COM')")) {
      ps.setObject(1, UUID.randomUUID());
      ps.setObject(2, requestId);
      ps.setString(3, gameUrl);
      ps.executeUpdate();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private void insertRawWithDedupeKey(String dedupeKey) {
    try (var conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement(
                "INSERT INTO indexing_requests (id, player, platform, start_month, end_month,"
                    + " dedupe_key) VALUES (?, 'constrained', 'CHESS_COM', '2025-11', '2025-11',"
                    + " ?)")) {
      ps.setObject(1, UUID.randomUUID());
      ps.setString(2, dedupeKey);
      ps.executeUpdate();
    } catch (java.sql.SQLException e) {
      throw new RuntimeException(e);
    }
  }

  private int countLiveRowsFor(String player) {
    try (var conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement(
                "SELECT COUNT(*) FROM indexing_requests WHERE player = ? AND status IN"
                    + " ('PENDING', 'PROCESSING')")) {
      ps.setString(1, player);
      try (var rs = ps.executeQuery()) {
        rs.next();
        return rs.getInt(1);
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private int countRequests() {
    try (var conn = dataSource.getConnection();
        var ps = conn.prepareStatement("SELECT COUNT(*) FROM indexing_requests");
        var rs = ps.executeQuery()) {
      rs.next();
      return rs.getInt(1);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }
}
