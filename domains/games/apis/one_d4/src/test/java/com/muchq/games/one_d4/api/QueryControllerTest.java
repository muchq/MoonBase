package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.GameFeatureRow;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.api.dto.QueryRequest;
import com.muchq.games.one_d4.api.dto.QueryResponse;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class QueryControllerTest {

  private QueryController controller;
  private FakeGameFeatureStore store;
  private MutableTicker ticker;
  private FirstPageCache cache;

  @BeforeEach
  public void setUp() {
    store = new FakeGameFeatureStore();
    ticker = new MutableTicker();
    cache = new FirstPageCache(ticker, FirstPageCache.MAX_AGE);
    controller =
        new QueryController(
            new QueryExecutor(store, new SqlCompiler()), new QueryRequestValidator(), cache);
  }

  private static QueryRequest defaultRequest() {
    return new QueryRequest(FirstPageCache.DEFAULT_QUERY, FirstPageCache.DEFAULT_LIMIT, 0, null);
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
                List.of(
                    new OccurrenceRow(
                        gameUrl,
                        "pin",
                        3,
                        "white",
                        "Knight pinned on c6",
                        null,
                        null,
                        null,
                        false,
                        false,
                        null)),
                "fork",
                List.of(
                    new OccurrenceRow(
                        gameUrl,
                        "fork",
                        10,
                        "white",
                        "Knight forks king and rook",
                        null,
                        null,
                        null,
                        false,
                        false,
                        null),
                    new OccurrenceRow(
                        gameUrl,
                        "fork",
                        18,
                        "black",
                        "Queen forks two pieces",
                        null,
                        null,
                        null,
                        false,
                        false,
                        null)))));

    QueryResponse response = controller.query(new QueryRequest("motif(pin)", 10, 0));

    assertThat(response.games()).hasSize(1);
    GameFeatureRow row = response.games().get(0);
    assertThat(row.gameUrl()).isEqualTo(gameUrl);
    assertThat(row.occurrences()).containsKey("pin");
    assertThat(row.occurrences().get("pin"))
        .containsExactly(
            new OccurrenceRow(
                gameUrl,
                "pin",
                3,
                "white",
                "Knight pinned on c6",
                null,
                null,
                null,
                false,
                false,
                null));
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
  public void query_perspectiveFieldWithoutPlayer_throws() {
    assertThatThrownBy(() -> controller.query(new QueryRequest("outcome = \"win\"", 10, 0)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("requires a player");
  }

  @Test
  public void query_perspectiveFieldWithPlayer_succeeds() {
    store.setQueryResult(List.of());
    store.setOccurrencesResult(Map.of());

    QueryResponse response =
        controller.query(new QueryRequest("outcome = \"win\"", 10, 0, "hikaru"));

    assertThat(response.count()).isZero();
  }

  @Test
  public void query_defaultFirstPageRequest_secondRequestIsServedFromCacheWithoutStoreQuery() {
    String gameUrl = "https://chess.com/game/cached";
    store.setQueryResult(List.of(createGameFeature(gameUrl)));
    store.setOccurrencesResult(Map.of(gameUrl, Map.of()));

    QueryResponse first = controller.query(defaultRequest());
    QueryResponse second = controller.query(defaultRequest());

    assertThat(store.queryCount()).isEqualTo(1);
    assertThat(second).isEqualTo(first);
    assertThat(second.games().get(0).gameUrl()).isEqualTo(gameUrl);
  }

  @Test
  public void query_defaultFirstPageRequest_staleCacheFallsThroughToStoreAndRewarms() {
    store.setQueryResult(List.of());
    store.setOccurrencesResult(Map.of());

    controller.query(defaultRequest());
    ticker.advance(FirstPageCache.MAX_AGE.plusSeconds(1));
    controller.query(defaultRequest());

    assertThat(store.queryCount()).isEqualTo(2);
    // The fall-through re-warmed the cache: a third request inside the window is served from it.
    controller.query(defaultRequest());
    assertThat(store.queryCount()).isEqualTo(2);
  }

  @Test
  public void query_nonDefaultRequests_bypassTheCacheEvenWhenWarm() {
    String cachedUrl = "https://chess.com/game/warmed-first";
    store.setQueryResult(List.of(createGameFeature(cachedUrl)));
    store.setOccurrencesResult(Map.of());

    controller.query(defaultRequest());
    assertThat(store.queryCount()).isEqualTo(1);

    // Same query, different page / page size / player: each is a different result set.
    store.setQueryResult(List.of(createGameFeature("https://chess.com/game/other")));
    controller.query(new QueryRequest(FirstPageCache.DEFAULT_QUERY, 25, 25, null));
    controller.query(new QueryRequest(FirstPageCache.DEFAULT_QUERY, 50, 0, null));
    controller.query(new QueryRequest(FirstPageCache.DEFAULT_QUERY, 25, 0, "hikaru"));
    controller.query(new QueryRequest("white_elo >= 2000", 25, 0, null));
    assertThat(store.queryCount()).isEqualTo(5);

    // The negative half: none of those bypass responses may have been written into the cache.
    // The default request must still see the originally warmed page, served without a 6th query.
    QueryResponse defaultAgain = controller.query(defaultRequest());
    assertThat(store.queryCount()).isEqualTo(5);
    assertThat(defaultAgain.games().get(0).gameUrl()).isEqualTo(cachedUrl);
  }

  @Test
  public void query_passesTheRequestsLimitAndOffsetToTheStore() {
    store.setQueryResult(List.of());
    store.setOccurrencesResult(Map.of());

    controller.query(new QueryRequest("white_elo >= 2000", 10, 7, null));

    assertThat(store.lastLimit()).isEqualTo(10);
    assertThat(store.lastOffset()).isEqualTo(7);
  }

  @Test
  public void query_defaultRequestWithSurroundingWhitespace_stillHitsCache() {
    store.setQueryResult(List.of());
    store.setOccurrencesResult(Map.of());

    controller.query(defaultRequest());
    controller.query(new QueryRequest("  " + FirstPageCache.DEFAULT_QUERY + " ", 25, 0, null));

    assertThat(store.queryCount()).isEqualTo(1);
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
        null,
        "GM",
        "blitz",
        "B90",
        "Sicilian Defense Najdorf Variation",
        "Sicilian Defense",
        "1-0",
        Instant.now(),
        30,
        Instant.now(),
        "pgn");
  }
}
