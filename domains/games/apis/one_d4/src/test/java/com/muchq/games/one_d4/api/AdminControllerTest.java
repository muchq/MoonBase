package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.api.dto.ReanalysisResponse;
import com.muchq.games.one_d4.api.dto.RederiveResponse;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.GameFeatureStore.GameOpening;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.GameReplayer;
import com.muchq.games.one_d4.engine.PgnParser;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.time.Instant;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import org.junit.jupiter.api.Test;

public class AdminControllerTest {

  // === Tests ===

  @Test
  public void reanalyze_emptyStore_returnsZeroCounts() {
    FakeGameFeatureStore store = new FakeGameFeatureStore();
    AdminController controller = new AdminController(store, noOpExtractor());

    ReanalysisResponse response = controller.reanalyze();

    assertThat(response.gamesProcessed()).isEqualTo(0);
    assertThat(response.gamesFailed()).isEqualTo(0);
  }

  @Test
  public void reanalyze_singleValidGame_returnsOneProcessed() {
    FakeGameFeatureStore store = new FakeGameFeatureStore();
    store.addGame("https://chess.com/game/1", "valid pgn");
    AdminController controller = new AdminController(store, noOpExtractor());

    ReanalysisResponse response = controller.reanalyze();

    assertThat(response.gamesProcessed()).isEqualTo(1);
    assertThat(response.gamesFailed()).isEqualTo(0);
  }

  @Test
  public void reanalyze_nullPgn_countsAsFailed_extractorNeverCalled() {
    FakeGameFeatureStore store = new FakeGameFeatureStore();
    store.addGame("https://chess.com/game/1", null);
    FakeFeatureExtractor extractor = noOpExtractor();
    AdminController controller = new AdminController(store, extractor);

    ReanalysisResponse response = controller.reanalyze();

    assertThat(response.gamesProcessed()).isEqualTo(0);
    assertThat(response.gamesFailed()).isEqualTo(1);
    assertThat(extractor.callCount()).isEqualTo(0);
  }

  @Test
  public void reanalyze_blankPgn_countsAsFailed() {
    FakeGameFeatureStore store = new FakeGameFeatureStore();
    store.addGame("https://chess.com/game/1", "   ");
    AdminController controller = new AdminController(store, noOpExtractor());

    ReanalysisResponse response = controller.reanalyze();

    assertThat(response.gamesProcessed()).isEqualTo(0);
    assertThat(response.gamesFailed()).isEqualTo(1);
  }

  @Test
  public void reanalyze_exceptionOnOneGame_othersStillProcessed() {
    // Null PGN game is sandwiched between two valid games.
    FakeGameFeatureStore store = new FakeGameFeatureStore();
    store.addGame("https://chess.com/game/1", "valid pgn 1");
    store.addGame("https://chess.com/game/2", null);
    store.addGame("https://chess.com/game/3", "valid pgn 3");
    AdminController controller = new AdminController(store, noOpExtractor());

    ReanalysisResponse response = controller.reanalyze();

    assertThat(response.gamesProcessed()).isEqualTo(2);
    assertThat(response.gamesFailed()).isEqualTo(1);
    // Game 3 is processed even though game 2 failed.
    assertThat(store.insertOccurrencesCount("https://chess.com/game/3")).isEqualTo(1);
  }

  @Test
  public void reanalyze_validGame_callsDeleteAndInsert() {
    FakeGameFeatureStore store = new FakeGameFeatureStore();
    String url = "https://chess.com/game/1";
    store.addGame(url, "valid pgn");
    AdminController controller = new AdminController(store, noOpExtractor());

    controller.reanalyze();

    assertThat(store.deleteOccurrencesCount(url)).isEqualTo(1);
    assertThat(store.insertOccurrencesCount(url)).isEqualTo(1);
  }

  @Test
  public void reanalyze_nullPgn_doesNotCallDeleteOrInsert() {
    FakeGameFeatureStore store = new FakeGameFeatureStore();
    String url = "https://chess.com/game/1";
    store.addGame(url, null);
    AdminController controller = new AdminController(store, noOpExtractor());

    controller.reanalyze();

    assertThat(store.deleteOccurrencesCount(url)).isEqualTo(0);
    assertThat(store.insertOccurrencesCount(url)).isEqualTo(0);
  }

  @Test
  public void reanalyze_multipleGames_correctAggregateCounts() {
    FakeGameFeatureStore store = new FakeGameFeatureStore();
    for (int i = 0; i < 5; i++) {
      store.addGame("https://chess.com/game/" + i, "pgn " + i);
    }
    AdminController controller = new AdminController(store, noOpExtractor());

    ReanalysisResponse response = controller.reanalyze();

    assertThat(response.gamesProcessed()).isEqualTo(5);
    assertThat(response.gamesFailed()).isEqualTo(0);
  }

  // === Helpers ===

  private static FakeFeatureExtractor noOpExtractor() {
    return new FakeFeatureExtractor();
  }

  private static class FakeFeatureExtractor extends FeatureExtractor {
    private int callCount = 0;

    FakeFeatureExtractor() {
      super(new PgnParser(), new GameReplayer(), List.of());
    }

    @Override
    public GameFeatures extract(String pgn) {
      callCount++;
      return new GameFeatures(Set.of(), 0, Map.of());
    }

    int callCount() {
      return callCount;
    }
  }

  /**
   * Correcting a derivation like #1344's takes a `skipCache` reindex today, which refetches every
   * month from chess.com to recompute something the table already determines: opening_family is
   * Openings.familyFromName(opening_name), and opening_name is stored per row. These pin the local
   * pass that replaces that round trip (#1350).
   */
  @Test
  public void rederiveOpenings_rewritesOnlyTheRowsWhoseFamilyIsStale() {
    FakeGameFeatureStore store = new FakeGameFeatureStore();
    // Stale in exactly the way #1344 left rows: the family kept the move continuation.
    store.addOpening("g1", "Owens Defense 3.Nc3 e6", "Owens Defense...3.Nc3-e6");
    // Already correct — must not be rewritten, or "updated" stops meaning anything.
    store.addOpening("g2", "Caro Kann Defense Two Knights Attack", "Caro Kann Defense");
    AdminController controller = new AdminController(store, noOpExtractor());

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
    AdminController controller = new AdminController(store, noOpExtractor());

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
    AdminController controller = new AdminController(store, noOpExtractor());

    RederiveResponse response = controller.rederiveOpenings();

    assertThat(response.gamesUpdated()).isEqualTo(2);
    assertThat(store.familyOf("g1")).isNull();
    assertThat(store.familyOf("g2")).isEqualTo("Birds Opening");
  }

  @Test
  public void rederiveOpenings_emptyStoreReportsNothing() {
    RederiveResponse response =
        new AdminController(new FakeGameFeatureStore(), noOpExtractor()).rederiveOpenings();

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
    AdminController controller = new AdminController(store, noOpExtractor());

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

    private final List<GameForReanalysis> games = new ArrayList<>();
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

    void addGame(String url, String pgn) {
      games.add(new GameForReanalysis(UUID.randomUUID(), url, pgn));
    }

    int deleteOccurrencesCount(String url) {
      return deleteCount.getOrDefault(url, 0);
    }

    int insertOccurrencesCount(String url) {
      return insertCount.getOrDefault(url, 0);
    }

    @Override
    public List<GameForReanalysis> fetchForReanalysis(int limit, int offset) {
      int start = Math.min(offset, games.size());
      int end = Math.min(offset + limit, games.size());
      return new ArrayList<>(games.subList(start, end));
    }

    @Override
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

    @Override
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
