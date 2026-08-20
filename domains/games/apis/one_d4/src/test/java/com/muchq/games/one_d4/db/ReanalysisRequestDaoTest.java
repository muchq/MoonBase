package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.Optional;
import java.util.UUID;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class ReanalysisRequestDaoTest {

  private ReanalysisRequestDao dao;
  private TestDb testDb;

  @BeforeEach
  public void setUp() {
    testDb = TestDb.create("reanalysis_req_test");
    dao = new ReanalysisRequestDao(testDb.jdbi());
  }

  @Test
  public void enqueue_emptyTable_createsAPendingPass() {
    ReanalysisRequestDao.EnqueueResult result = dao.enqueue();

    assertThat(result.created()).isTrue();
    assertThat(result.request().status()).isEqualTo("PENDING");
    assertThat(result.request().gamesProcessed()).isZero();
    assertThat(result.request().gamesFailed()).isZero();
    assertThat(result.request().errorMessage()).isNull();
  }

  @Test
  public void enqueue_whileAPassIsLive_returnsItInsteadOfStackingASecond() {
    // One pass walks the whole corpus, so a second live row buys nothing and
    // idx_reanalysis_requests_single_live refuses it at insert on Postgres.
    // The dao answers the same way on both engines: here is the pass that is
    // already doing what you asked for.
    UUID first = dao.enqueue().request().id();

    ReanalysisRequestDao.EnqueueResult again = dao.enqueue();
    assertThat(again.created()).isFalse();
    assertThat(again.request().id()).isEqualTo(first);
  }

  @Test
  public void enqueue_afterThePassFinishes_createsAFreshOne() {
    UUID first = dao.enqueue().request().id();
    testDb
        .jdbi()
        .useHandle(
            h ->
                h.createUpdate("UPDATE reanalysis_requests SET status = 'COMPLETED' WHERE id = :id")
                    .bind("id", first)
                    .execute());

    ReanalysisRequestDao.EnqueueResult next = dao.enqueue();
    assertThat(next.created()).isTrue();
    assertThat(next.request().id()).isNotEqualTo(first);
  }

  @Test
  public void enqueue_aFailedPassDoesNotHoldTheSlot() {
    UUID first = dao.enqueue().request().id();
    testDb
        .jdbi()
        .useHandle(
            h ->
                h.createUpdate("UPDATE reanalysis_requests SET status = 'FAILED' WHERE id = :id")
                    .bind("id", first)
                    .execute());

    assertThat(dao.enqueue().created()).isTrue();
  }

  @Test
  public void findById_reportsWhatTheWorkerWrote() {
    UUID id = dao.enqueue().request().id();
    testDb
        .jdbi()
        .useHandle(
            h ->
                h.createUpdate(
                        "UPDATE reanalysis_requests SET status = 'COMPLETED',"
                            + " games_processed = 120, games_failed = 3 WHERE id = :id")
                    .bind("id", id)
                    .execute());

    Optional<ReanalysisRequestDao.ReanalysisRequest> found = dao.findById(id);
    assertThat(found).isPresent();
    assertThat(found.get().status()).isEqualTo("COMPLETED");
    assertThat(found.get().gamesProcessed()).isEqualTo(120);
    assertThat(found.get().gamesFailed()).isEqualTo(3);
  }

  @Test
  public void findById_unknownId_isEmpty() {
    assertThat(dao.findById(UUID.randomUUID())).isEmpty();
  }
}
