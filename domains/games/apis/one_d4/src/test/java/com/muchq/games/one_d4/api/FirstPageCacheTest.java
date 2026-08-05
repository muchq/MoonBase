package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.one_d4.api.dto.QueryRequest;
import com.muchq.games.one_d4.api.dto.QueryResponse;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class FirstPageCacheTest {

  private FakeGameFeatureStore store;
  private MutableTicker ticker;
  private FirstPageCache cache;

  @BeforeEach
  public void setUp() {
    store = new FakeGameFeatureStore();
    ticker = new MutableTicker();
    cache =
        new FirstPageCache(
            ticker, FirstPageCache.MAX_AGE, new QueryExecutor(store, new SqlCompiler()));
  }

  @Test
  public void matches_theExactFirstLoadRequest() {
    assertThat(cache.matches(FirstPageCache.defaultRequest())).isTrue();
    assertThat(cache.matches(new QueryRequest("num.moves >= 0", 25, 0, null))).isTrue();
  }

  @Test
  public void matches_toleratesSurroundingWhitespaceAndBlankPlayer() {
    assertThat(cache.matches(new QueryRequest("  num.moves >= 0  ", 25, 0, null))).isTrue();
    assertThat(cache.matches(new QueryRequest("num.moves >= 0", 25, 0, ""))).isTrue();
    assertThat(cache.matches(new QueryRequest("num.moves >= 0", 25, 0, "  "))).isTrue();
  }

  @Test
  public void matches_rejectsAnythingThatIsADifferentResultSet() {
    assertThat(cache.matches(new QueryRequest("white_elo >= 2000", 25, 0, null)))
        .as("different query")
        .isFalse();
    assertThat(cache.matches(new QueryRequest("num.moves >= 0", 50, 0, null)))
        .as("different page size")
        .isFalse();
    assertThat(cache.matches(new QueryRequest("num.moves >= 0", 25, 25, null)))
        .as("different page")
        .isFalse();
    assertThat(cache.matches(new QueryRequest("num.moves >= 0", 25, 0, "hikaru")))
        .as(
            "a player cannot change this query's results today, but rejecting it keeps matches()"
                + " a pure request predicate rather than a claim about ChessQL semantics")
        .isFalse();
    assertThat(cache.matches(new QueryRequest(null, 25, 0, null))).as("null query").isFalse();
  }

  /**
   * The cross-language gate: this fixture is also POSTed verbatim by FirstPageWarmupTest and read
   * by 1d4_web's GamesView.test.tsx. If the Java constants drift from it, this test fails; if the
   * frontend default drifts from it, the frontend suite fails. Neither side can silently lose the
   * fast path by editing only its own literal.
   */
  @Test
  public void sharedWireFixtureMatchesTheJavaConstants() throws Exception {
    try (java.io.InputStream in =
        FirstPageCacheTest.class.getResourceAsStream("/first_page_request.json")) {
      assertThat(in).as("fixture missing from test resources").isNotNull();
      com.fasterxml.jackson.databind.JsonNode fixture =
          new com.fasterxml.jackson.databind.ObjectMapper().readTree(in);
      assertThat(fixture.get("query").asText()).isEqualTo(FirstPageCache.DEFAULT_QUERY);
      assertThat(fixture.get("limit").asInt()).isEqualTo(FirstPageCache.DEFAULT_LIMIT);
      assertThat(fixture.get("offset").asInt()).isEqualTo(0);
      assertThat(fixture.has("player")).as("the first-load request sends no player").isFalse();
    }
  }

  @Test
  public void get_loadsExactlyTheDefaultRequestOnAMiss() {
    cache.get();

    assertThat(store.queryCount()).isEqualTo(1);
    assertThat(store.lastLimit()).isEqualTo(FirstPageCache.DEFAULT_LIMIT);
    assertThat(store.lastOffset()).isEqualTo(0);
    assertThat(store.lastCompiled())
        .isEqualTo(
            new SqlCompiler()
                .compile(
                    com.muchq.games.chessql.parser.Parser.parse(FirstPageCache.DEFAULT_QUERY),
                    null));
  }

  @Test
  public void get_servesTheSnapshotWithoutReloadingWhileFresh() {
    QueryResponse first = cache.get();
    ticker.advance(FirstPageCache.MAX_AGE.minusSeconds(1));
    QueryResponse second = cache.get();

    assertThat(store.queryCount()).isEqualTo(1);
    assertThat(second).isEqualTo(first);
  }

  @Test
  public void get_reloadsOnceExpired() {
    cache.get();
    // Caffeine's expireAfterWrite expires the entry once its age reaches the duration.
    ticker.advance(FirstPageCache.MAX_AGE);
    cache.get();

    assertThat(store.queryCount()).isEqualTo(2);
  }

  @Test
  public void get_propagatesALoaderFailureLikeTheLivePath() {
    store.failQueriesWith(new RuntimeException("db down"));

    assertThatThrownBy(() -> cache.get()).hasMessageContaining("db down");
    assertThat(cache.peek()).as("a failed load caches nothing").isEmpty();
  }

  @Test
  public void refreshNow_recomputesEvenWhileFresh() {
    cache.get();
    cache.refreshNow();

    assertThat(store.queryCount()).isEqualTo(2);
  }

  /**
   * The reason refreshNow() is a put and not LoadingCache.refresh: refresh discards a reload whose
   * entry changed underneath it, and expiring mid-reload counts. A tick whose query outlives the
   * remaining freshness window must still install its (successful) result rather than silently
   * leaving the cache cold until the next tick.
   */
  @Test
  public void refreshNow_slowTickWhoseEntryExpiresMidLoadStillInstallsTheResult() {
    cache.get();

    // The next store query simulates a load so slow the warmed entry expires while it runs.
    store.onQuery(() -> ticker.advance(FirstPageCache.MAX_AGE.plusSeconds(1)));
    cache.refreshNow();

    assertThat(cache.peek())
        .as("a successful warm installs even when the prior entry expired mid-load")
        .isPresent();
  }

  @Test
  public void refreshNow_failureKeepsTheEarlierSnapshotAndDoesNotThrow() {
    QueryResponse warmed = cache.get();

    store.failQueriesWith(new RuntimeException("db down"));
    cache.refreshNow();

    assertThat(cache.peek())
        .as("a failed refresh keeps the last good snapshot until MAX_AGE expires it")
        .contains(warmed);
  }
}
