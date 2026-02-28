package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.api.dto.ReanalysisResponse;
import com.muchq.games.one_d4.db.GameFeatureStore;
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
import org.junit.Test;

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
  public void reanalyze_updateMotifsThrows_countsAsFailed() {
    FakeGameFeatureStore store = new FakeGameFeatureStore();
    store.addGame("https://chess.com/game/1", "valid pgn");
    store.throwOnUpdateMotifs = true;
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
    assertThat(store.updateMotifsCount("https://chess.com/game/3")).isEqualTo(1);
  }

  @Test
  public void reanalyze_validGame_callsUpdateDeleteInsert() {
    FakeGameFeatureStore store = new FakeGameFeatureStore();
    String url = "https://chess.com/game/1";
    store.addGame(url, "valid pgn");
    AdminController controller = new AdminController(store, noOpExtractor());

    controller.reanalyze();

    assertThat(store.updateMotifsCount(url)).isEqualTo(1);
    assertThat(store.deleteOccurrencesCount(url)).isEqualTo(1);
    assertThat(store.insertOccurrencesCount(url)).isEqualTo(1);
  }

  @Test
  public void reanalyze_nullPgn_doesNotCallUpdateOrDelete() {
    FakeGameFeatureStore store = new FakeGameFeatureStore();
    String url = "https://chess.com/game/1";
    store.addGame(url, null);
    AdminController controller = new AdminController(store, noOpExtractor());

    controller.reanalyze();

    assertThat(store.updateMotifsCount(url)).isEqualTo(0);
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

  private static class FakeGameFeatureStore implements GameFeatureStore {
    private final List<GameForReanalysis> games = new ArrayList<>();
    private final Map<String, Integer> updateMotifsCount = new HashMap<>();
    private final Map<String, Integer> deleteCount = new HashMap<>();
    private final Map<String, Integer> insertCount = new HashMap<>();
    boolean throwOnUpdateMotifs = false;

    void addGame(String url, String pgn) {
      games.add(new GameForReanalysis(UUID.randomUUID(), url, pgn));
    }

    int updateMotifsCount(String url) {
      return updateMotifsCount.getOrDefault(url, 0);
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
    public void updateMotifs(String gameUrl, GameFeatures features) {
      if (throwOnUpdateMotifs) throw new RuntimeException("store error");
      updateMotifsCount.merge(gameUrl, 1, Integer::sum);
    }

    @Override
    public void deleteOccurrencesByGameUrl(String gameUrl) {
      deleteCount.merge(gameUrl, 1, Integer::sum);
    }

    @Override
    public void insertOccurrences(
        String gameUrl, Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences) {
      insertCount.merge(gameUrl, 1, Integer::sum);
    }

    @Override
    public void insert(GameFeature feature) {}

    @Override
    public int deleteOlderThan(Instant threshold) {
      return 0;
    }

    @Override
    public List<GameFeature> query(Object compiledQuery, int limit, int offset) {
      return List.of();
    }

    @Override
    public Map<String, Map<String, List<OccurrenceRow>>> queryOccurrences(List<String> gameUrls) {
      return Map.of();
    }
  }
}
