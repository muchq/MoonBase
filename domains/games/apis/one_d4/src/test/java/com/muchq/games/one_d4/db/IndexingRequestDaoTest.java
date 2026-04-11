package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.List;
import java.util.Optional;
import java.util.UUID;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class IndexingRequestDaoTest {

  private IndexingRequestDao dao;

  @BeforeEach
  public void setUp() {
    dao = new IndexingRequestDao(TestDb.create("index_req_test").jdbi());
  }

  @Test
  public void findExistingRequest_returnsEmptyWhenNoRequests() {
    Optional<IndexingRequestStore.IndexingRequest> result =
        dao.findExistingRequest("player1", "CHESS_COM", "2024-01", "2024-01", false);
    assertThat(result).isEmpty();
  }

  @Test
  public void findExistingRequest_returnsPendingRequestWithMatchingParams() {
    UUID id = dao.create("player1", "CHESS_COM", "2024-01", "2024-03", false);
    Optional<IndexingRequestStore.IndexingRequest> result =
        dao.findExistingRequest("player1", "CHESS_COM", "2024-01", "2024-03", false);
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
    UUID id = dao.create("player2", "CHESS_COM", "2024-02", "2024-02", false);
    dao.updateStatus(id, "PROCESSING", null, 5);
    Optional<IndexingRequestStore.IndexingRequest> result =
        dao.findExistingRequest("player2", "CHESS_COM", "2024-02", "2024-02", false);
    assertThat(result).isPresent();
    assertThat(result.get().id()).isEqualTo(id);
    assertThat(result.get().status()).isEqualTo("PROCESSING");
    assertThat(result.get().gamesIndexed()).isEqualTo(5);
  }

  @Test
  public void findExistingRequest_ignoresCompletedRequest() {
    UUID id = dao.create("player3", "CHESS_COM", "2024-03", "2024-03", false);
    dao.updateStatus(id, "COMPLETED", null, 10);
    Optional<IndexingRequestStore.IndexingRequest> result =
        dao.findExistingRequest("player3", "CHESS_COM", "2024-03", "2024-03", false);
    assertThat(result).isEmpty();
  }

  @Test
  public void findExistingRequest_ignoresFailedRequest() {
    UUID id = dao.create("player4", "CHESS_COM", "2024-04", "2024-04", false);
    dao.updateStatus(id, "FAILED", "Network error", 0);
    Optional<IndexingRequestStore.IndexingRequest> result =
        dao.findExistingRequest("player4", "CHESS_COM", "2024-04", "2024-04", false);
    assertThat(result).isEmpty();
  }

  @Test
  public void findExistingRequest_returnsOldestWhenMultiplePending() {
    UUID first = dao.create("same", "CHESS_COM", "2024-01", "2024-01", false);
    dao.create("same", "CHESS_COM", "2024-01", "2024-01", false);
    Optional<IndexingRequestStore.IndexingRequest> result =
        dao.findExistingRequest("same", "CHESS_COM", "2024-01", "2024-01", false);
    assertThat(result).isPresent();
    assertThat(result.get().id()).isEqualTo(first);
  }

  @Test
  public void listRecent_returnsEmptyWhenNoRequests() {
    List<IndexingRequestStore.IndexingRequest> results = dao.listRecent(10);
    assertThat(results).isEmpty();
  }

  @Test
  public void listRecent_returnsRequestsOrderedByCreatedAtDesc() {
    UUID first = dao.create("playerA", "CHESS_COM", "2024-01", "2024-01", false);
    UUID second = dao.create("playerB", "CHESS_COM", "2024-02", "2024-02", false);
    UUID third = dao.create("playerC", "CHESS_COM", "2024-03", "2024-03", false);

    List<IndexingRequestStore.IndexingRequest> results = dao.listRecent(10);

    assertThat(results).hasSize(3);
    assertThat(results.get(0).id()).isEqualTo(third);
    assertThat(results.get(1).id()).isEqualTo(second);
    assertThat(results.get(2).id()).isEqualTo(first);
  }

  @Test
  public void listRecent_respectsLimit() {
    dao.create("playerA", "CHESS_COM", "2024-01", "2024-01", false);
    dao.create("playerB", "CHESS_COM", "2024-02", "2024-02", false);
    dao.create("playerC", "CHESS_COM", "2024-03", "2024-03", false);

    List<IndexingRequestStore.IndexingRequest> results = dao.listRecent(2);

    assertThat(results).hasSize(2);
  }

  @Test
  public void findExistingRequest_returnsEmptyWhenParamsDiffer() {
    dao.create("player5", "CHESS_COM", "2024-01", "2024-01", false);
    Optional<IndexingRequestStore.IndexingRequest> result =
        dao.findExistingRequest("player5", "CHESS_COM", "2024-02", "2024-02", false);
    assertThat(result).isEmpty();
  }

  @Test
  public void findExistingRequest_distinguishesByExcludeBullet() {
    UUID id = dao.create("player6", "CHESS_COM", "2024-05", "2024-05", true);

    // Same params, same excludeBullet → match
    Optional<IndexingRequestStore.IndexingRequest> match =
        dao.findExistingRequest("player6", "CHESS_COM", "2024-05", "2024-05", true);
    assertThat(match).isPresent();
    assertThat(match.get().id()).isEqualTo(id);
    assertThat(match.get().excludeBullet()).isTrue();

    // Same params, different excludeBullet → no match
    Optional<IndexingRequestStore.IndexingRequest> noMatch =
        dao.findExistingRequest("player6", "CHESS_COM", "2024-05", "2024-05", false);
    assertThat(noMatch).isEmpty();
  }
}
