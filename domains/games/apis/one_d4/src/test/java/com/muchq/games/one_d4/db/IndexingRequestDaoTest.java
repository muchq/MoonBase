package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.Optional;
import java.util.UUID;
import javax.sql.DataSource;
import org.junit.Before;
import org.junit.Test;

public class IndexingRequestDaoTest {

  private IndexingRequestDao dao;

  @Before
  public void setUp() {
    String jdbcUrl = "jdbc:h2:mem:index_req_test;DB_CLOSE_DELAY=-1";
    DataSource dataSource = DataSourceFactory.create(jdbcUrl, "sa", "");
    Migration migration = new Migration(dataSource, true);
    migration.run();
    dao = new IndexingRequestDao(dataSource);
  }

  @Test
  public void findExistingRequest_returnsEmptyWhenNoRequests() {
    Optional<IndexingRequestStore.IndexingRequest> result =
        dao.findExistingRequest("player1", "CHESS_COM", "2024-01", "2024-01");
    assertThat(result).isEmpty();
  }

  @Test
  public void findExistingRequest_returnsPendingRequestWithMatchingParams() {
    UUID id = dao.create("player1", "CHESS_COM", "2024-01", "2024-03");
    Optional<IndexingRequestStore.IndexingRequest> result =
        dao.findExistingRequest("player1", "CHESS_COM", "2024-01", "2024-03");
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
    UUID id = dao.create("player2", "CHESS_COM", "2024-02", "2024-02");
    dao.updateStatus(id, "PROCESSING", null, 5);
    Optional<IndexingRequestStore.IndexingRequest> result =
        dao.findExistingRequest("player2", "CHESS_COM", "2024-02", "2024-02");
    assertThat(result).isPresent();
    assertThat(result.get().id()).isEqualTo(id);
    assertThat(result.get().status()).isEqualTo("PROCESSING");
    assertThat(result.get().gamesIndexed()).isEqualTo(5);
  }

  @Test
  public void findExistingRequest_ignoresCompletedRequest() {
    UUID id = dao.create("player3", "CHESS_COM", "2024-03", "2024-03");
    dao.updateStatus(id, "COMPLETED", null, 10);
    Optional<IndexingRequestStore.IndexingRequest> result =
        dao.findExistingRequest("player3", "CHESS_COM", "2024-03", "2024-03");
    assertThat(result).isEmpty();
  }

  @Test
  public void findExistingRequest_ignoresFailedRequest() {
    UUID id = dao.create("player4", "CHESS_COM", "2024-04", "2024-04");
    dao.updateStatus(id, "FAILED", "Network error", 0);
    Optional<IndexingRequestStore.IndexingRequest> result =
        dao.findExistingRequest("player4", "CHESS_COM", "2024-04", "2024-04");
    assertThat(result).isEmpty();
  }

  @Test
  public void findExistingRequest_returnsOldestWhenMultiplePending() {
    UUID first = dao.create("same", "CHESS_COM", "2024-01", "2024-01");
    dao.create("same", "CHESS_COM", "2024-01", "2024-01");
    Optional<IndexingRequestStore.IndexingRequest> result =
        dao.findExistingRequest("same", "CHESS_COM", "2024-01", "2024-01");
    assertThat(result).isPresent();
    assertThat(result.get().id()).isEqualTo(first);
  }

  @Test
  public void findExistingRequest_returnsEmptyWhenParamsDiffer() {
    dao.create("player5", "CHESS_COM", "2024-01", "2024-01");
    Optional<IndexingRequestStore.IndexingRequest> result =
        dao.findExistingRequest("player5", "CHESS_COM", "2024-02", "2024-02");
    assertThat(result).isEmpty();
  }
}
