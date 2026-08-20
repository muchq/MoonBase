package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.api.dto.ReanalysisRequestResponse;
import com.muchq.games.one_d4.api.dto.RederiveResponse;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.GameFeatureStore.GameOpening;
import com.muchq.games.one_d4.db.ReanalysisRequestDao;
import com.muchq.games.one_d4.db.TestDb;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.time.Instant;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.NoSuchElementException;
import java.util.UUID;
import org.junit.jupiter.api.Test;

public class AdminControllerTest {

  // === Tests ===

  // The reanalyze endpoint enqueues; the pass itself runs in the C++ worker.
  // A real dao over H2, because the interesting behavior — one live pass,
  // reuse instead of stacking — is the dao's, and a fake of it would just
  // restate the controller.

  @Test
  public void reanalyze_enqueuesAPendingPass() {
    AdminController controller = controller();

    ReanalysisRequestResponse response = controller.reanalyze();

    assertThat(response.id()).isNotNull();
    assertThat(response.status()).isEqualTo("PENDING");
    assertThat(response.gamesProcessed()).isZero();
    assertThat(response.gamesFailed()).isZero();
    assertThat(response.errorMessage()).isNull();
  }

  @Test
  public void reanalyze_whileAPassIsLive_answersWithItInsteadOfStackingASecond() {
    AdminController controller = controller();

    ReanalysisRequestResponse first = controller.reanalyze();
    ReanalysisRequestResponse again = controller.reanalyze();

    assertThat(again.id()).isEqualTo(first.id());
  }

  @Test
  public void getReanalysis_reportsWhatTheWorkerCheckpointed() {
    AdminController controller = controller();
    UUID id = controller.reanalyze().id();
    testDb
        .jdbi()
        .useHandle(
            h ->
                h.createUpdate(
                        "UPDATE reanalysis_requests SET status = 'PROCESSING',"
                            + " games_processed = 500, games_failed = 3 WHERE id = :id")
                    .bind("id", id)
                    .execute());

    ReanalysisRequestResponse response = controller.getReanalysis(id);

    assertThat(response.status()).isEqualTo("PROCESSING");
    assertThat(response.gamesProcessed()).isEqualTo(500);
    assertThat(response.gamesFailed()).isEqualTo(3);
  }

  @Test
  public void getReanalysis_unknownId_throwsNotFound() {
    AdminController controller = controller();

    assertThatThrownBy(() -> controller.getReanalysis(UUID.randomUUID()))
        .isInstanceOf(NoSuchElementException.class);
  }

  // === Helpers ===

  private TestDb testDb;

  private AdminController controller() {
    return controller(new FakeGameFeatureStore());
  }

  private AdminController controller(GameFeatureStore store) {
    testDb = TestDb.create("admin_ctrl_test");
    return new AdminController(store, new ReanalysisRequestDao(testDb.jdbi()));
  }

  @Test
  public void rederiveOpenings_rewritesOnlyTheRowsWhoseFamilyIsStale() {
    FakeGameFeatureStore store = new FakeGameFeatureStore();
    // Stale in exactly the way #1344 left rows: the family kept the move continuation.
    store.addOpening("g1", "Owens Defense 3.Nc3 e6", "Owens Defense...3.Nc3-e6");
    // Already correct — must not be rewritten, or "updated" stops meaning anything.
    store.addOpening("g2", "Caro Kann Defense Two Knights Attack", "Caro Kann Defense");
    AdminController controller = controller(store);

    RederiveResponse response = controller.rederiveOpenings();

    assertThat(response.gamesScanned()).isEqualTo(2);
    assertThat(response.gamesUpdated()).isEqualTo(1);
    assertThat(store.familyOf("g1")).isEqualTo("Owens Defense");
    assertThat(store.familyOf("g2")).isEqualTo("Caro Kann Defense");
    assertThat(store.writtenUrls()).containsExactly("g1");
  }

  /** The derivation is shared with the index path, so a second pass has nothing left to do. */
  @Test
  public void rederiveOpenings_isIdempotent() {
    FakeGameFeatureStore store = new FakeGameFeatureStore();
    store.addOpening("g1", "Owens Defense 3.Nc3 e6", "Owens Defense...3.Nc3-e6");
    AdminController controller = controller(store);

    assertThat(controller.rederiveOpenings().gamesUpdated()).isEqualTo(1);
    RederiveResponse second = controller.rederiveOpenings();

    assertThat(second.gamesScanned()).isEqualTo(1);
    assertThat(second.gamesUpdated()).isZero();
  }

  /**
   * A row whose name is NULL has no family, and re-deriving has to say so rather than leaving a
   * value the current derivation would never produce. The reverse — deriving a family for a row
   * that had none — is the same rule read the other way.
   */
  @Test
  public void rederiveOpenings_clearsAFamilyWhoseNameIsGone() {
    FakeGameFeatureStore store = new FakeGameFeatureStore();
    store.addOpening("g1", null, "Stale Family");
    store.addOpening("g2", "Birds Opening 2.Nf3", null);
    AdminController controller = controller(store);

    RederiveResponse response = controller.rederiveOpenings();

    assertThat(response.gamesUpdated()).isEqualTo(2);
    assertThat(store.familyOf("g1")).isNull();
    assertThat(store.familyOf("g2")).isEqualTo("Birds Opening");
  }

  @Test
  public void rederiveOpenings_emptyStoreReportsNothing() {
    RederiveResponse response = controller().rederiveOpenings();

    assertThat(response.gamesScanned()).isZero();
    assertThat(response.gamesUpdated()).isZero();
  }

  /**
   * More rows than one batch. The loop pages by offset while rewriting rows it has already read, so
   * a page that shifted underneath the cursor would skip rows silently — every row has to be seen
   * exactly once.
   */
  @Test
  public void rederiveOpenings_pagesPastTheBatchSize() {
    FakeGameFeatureStore store = new FakeGameFeatureStore();
    int rows = AdminController.BATCH_SIZE + 7;
    for (int i = 0; i < rows; i++) {
      store.addOpening("g" + i, "Owens Defense 3.Nc3 e6", "stale");
    }
    AdminController controller = controller(store);

    RederiveResponse response = controller.rederiveOpenings();

    assertThat(response.gamesScanned()).isEqualTo(rows);
    assertThat(response.gamesUpdated()).isEqualTo(rows);
    assertThat(store.writtenUrls()).hasSize(rows).doesNotHaveDuplicates();
  }

  private static class FakeGameFeatureStore implements GameFeatureStore {

    /** Not part of the AdminController surface: only the worker flushes. */
    @Override
    public boolean flushOwned(
        java.util.UUID requestId,
        String ownerId,
        Instant now,
        List<GameFeature> features,
        Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame) {
      throw new UnsupportedOperationException("AdminController tests never flush");
    }

    private final Map<String, Integer> deleteCount = new HashMap<>();
    private final Map<String, Integer> insertCount = new HashMap<>();
    private final List<GameOpening> openings = new ArrayList<>();
    private final List<String> written = new ArrayList<>();

    void addOpening(String url, String name, String family) {
      openings.add(new GameOpening(url, name, family));
    }

    String familyOf(String url) {
      return openings.stream()
          .filter(o -> o.gameUrl().equals(url))
          .findFirst()
          .orElseThrow()
          .openingFamily();
    }

    List<String> writtenUrls() {
      return List.copyOf(written);
    }

    @Override
    public List<GameOpening> fetchOpeningsForRederive(int limit, int offset) {
      int start = Math.min(offset, openings.size());
      int end = Math.min(offset + limit, openings.size());
      return List.copyOf(openings.subList(start, end));
    }

    @Override
    public int updateOpeningFamilies(List<GameOpening> updates) {
      for (GameOpening update : updates) {
        written.add(update.gameUrl());
        openings.replaceAll(
            existing ->
                existing.gameUrl().equals(update.gameUrl())
                    ? new GameOpening(
                        existing.gameUrl(), existing.openingName(), update.openingFamily())
                    : existing);
      }
      return updates.size();
    }

    int deleteOccurrencesCount(String url) {
      return deleteCount.getOrDefault(url, 0);
    }

    int insertOccurrencesCount(String url) {
      return insertCount.getOrDefault(url, 0);
    }

    public void deleteOccurrencesByGameUrls(List<String> gameUrls) {
      for (String url : gameUrls) {
        deleteCount.merge(url, 1, Integer::sum);
      }
    }

    @Override
    public void insertBatch(List<GameFeature> features) {}

    @Override
    public int deleteOlderThan(Instant threshold) {
      return 0;
    }

    public void insertOccurrencesBatch(
        Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame) {
      for (String url : occurrencesByGame.keySet()) {
        insertCount.merge(url, 1, Integer::sum);
      }
    }

    @Override
    public List<GameFeature> query(Object compiledQuery, int limit, int offset) {
      return List.of();
    }

    @Override
    public List<com.muchq.games.one_d4.api.dto.AggregateRow> aggregate(
        Object compiledQuery, List<String> groupColumns, boolean withOutcomeMetrics, int limit) {
      return List.of();
    }

    @Override
    public AggregateTotals aggregateTotals(Object compiledQuery) {
      return new AggregateTotals(0, 0);
    }

    @Override
    public Map<String, Map<String, List<OccurrenceRow>>> queryOccurrences(List<String> gameUrls) {
      return Map.of();
    }
  }
}
