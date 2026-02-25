package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.GameFeatureRow;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.api.dto.QueryRequest;
import com.muchq.games.one_d4.api.dto.QueryResponse;
import com.muchq.games.one_d4.db.GameFeatureStore;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class QueryControllerTest {

  private QueryController controller;
  private FakeGameFeatureStore store;

  @Before
  public void setUp() {
    store = new FakeGameFeatureStore();
    controller = new QueryController(store, new SqlCompiler(), new QueryRequestValidator());
  }

  @Test
  public void query_returnsGamesWithOccurrencesMappedByGameUrl() {
    String gameUrl = "https://chess.com/game/with-motifs";
    GameFeature feature = createGameFeature(gameUrl);
    store.setQueryResult(List.of(feature));
    store.setOccurrencesResult(
        Map.of(
            gameUrl,
            Map.of(
                "pin",
                List.of(new OccurrenceRow(3, "white", "Knight pinned on c6")),
                "fork",
                List.of(
                    new OccurrenceRow(10, "white", "Knight forks king and rook"),
                    new OccurrenceRow(18, "black", "Queen forks two pieces")))));

    QueryResponse response = controller.query(new QueryRequest("motif(pin)", 10, 0));

    assertThat(response.games()).hasSize(1);
    GameFeatureRow row = response.games().get(0);
    assertThat(row.gameUrl()).isEqualTo(gameUrl);
    assertThat(row.occurrences()).containsKey("pin");
    assertThat(row.occurrences().get("pin"))
        .containsExactly(new OccurrenceRow(3, "white", "Knight pinned on c6"));
    assertThat(row.occurrences()).containsKey("fork");
    assertThat(row.occurrences().get("fork")).hasSize(2);
    assertThat(response.count()).isEqualTo(1);
  }

  @Test
  public void query_whenNoOccurrences_returnsEmptyOccurrencesMapPerGame() {
    String gameUrl = "https://chess.com/game/no-motifs";
    store.setQueryResult(List.of(createGameFeature(gameUrl)));
    store.setOccurrencesResult(Map.of(gameUrl, Map.of()));

    QueryResponse response = controller.query(new QueryRequest("white_elo >= 2000", 10, 0));

    assertThat(response.games()).hasSize(1);
    assertThat(response.games().get(0).occurrences()).isEmpty();
  }

  @Test
  public void query_whenStoreReturnsEmptyList_returnsEmptyResponse() {
    store.setQueryResult(List.of());
    store.setOccurrencesResult(Map.of());

    QueryResponse response = controller.query(new QueryRequest("motif(fork)", 10, 0));

    assertThat(response.games()).isEmpty();
    assertThat(response.count()).isEqualTo(0);
  }

  @Test
  public void query_blankQuery_throws() {
    assertThatThrownBy(() -> controller.query(new QueryRequest("  ", 10, 0)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("query is required");
  }

  @Test
  public void query_nullQuery_throws() {
    assertThatThrownBy(() -> controller.query(new QueryRequest(null, 10, 0)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("query is required");
  }

  private static GameFeature createGameFeature(String gameUrl) {
    return new GameFeature(
        UUID.randomUUID(),
        UUID.randomUUID(),
        gameUrl,
        "CHESS_COM",
        "white",
        "black",
        2000,
        1900,
        "blitz",
        "B90",
        "1-0",
        Instant.now(),
        30,
        true,
        false,
        true,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        Instant.now(),
        "pgn");
  }

  private static final class FakeGameFeatureStore implements GameFeatureStore {
    private List<GameFeature> queryResult = List.of();
    private Map<String, Map<String, List<OccurrenceRow>>> occurrencesResult = Map.of();

    void setQueryResult(List<GameFeature> result) {
      this.queryResult = result;
    }

    void setOccurrencesResult(Map<String, Map<String, List<OccurrenceRow>>> result) {
      this.occurrencesResult = result == null ? Map.of() : result;
    }

    @Override
    public void insert(GameFeature feature) {}

    @Override
    public void deleteOlderThan(java.time.Instant threshold) {}

    @Override
    public void insertOccurrences(
        String gameUrl,
        java.util.Map<
                com.muchq.games.one_d4.engine.model.Motif,
                List<com.muchq.games.one_d4.engine.model.GameFeatures.MotifOccurrence>>
            occurrences) {}

    @Override
    public List<GameFeature> query(Object compiledQuery, int limit, int offset) {
      return queryResult;
    }

    @Override
    public Map<String, Map<String, List<OccurrenceRow>>> queryOccurrences(List<String> gameUrls) {
      if (gameUrls.isEmpty()) return Map.of();
      java.util.Map<String, Map<String, List<OccurrenceRow>>> out = new java.util.LinkedHashMap<>();
      for (String url : gameUrls) {
        out.put(url, occurrencesResult.getOrDefault(url, Map.of()));
      }
      return out;
    }
  }
}
