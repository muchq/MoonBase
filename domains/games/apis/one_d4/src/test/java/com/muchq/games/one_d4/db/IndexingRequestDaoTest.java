package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

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
    backdateCreatedAt(old.request().id(), now.minus(Duration.ofDays(40)));

    assertThat(dao.deleteOlderThan(now.minus(Duration.ofDays(30)))).isEqualTo(1);

    assertThat(dao.findById(old.request().id())).isEmpty();
    assertThat(dao.findById(recent.request().id())).isPresent();
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
