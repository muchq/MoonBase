package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.one_d4.api.dto.AggregateRequest;
import com.muchq.games.one_d4.api.dto.AggregateResponse;
import com.muchq.games.one_d4.api.dto.AggregateRow;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.time.Instant;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;

public class AggregateControllerTest {

  private final RecordingStore store = new RecordingStore();
  private final AggregateController controller =
      new AggregateController(store, new SqlCompiler(), new AggregateRequestValidator());

  @Test
  public void aggregate_compilesQueryAndMapsGroups() {
    store.rows =
        List.of(
            new AggregateRow(Map.of("opening_family", "Caro Kann Defense"), 42),
            new AggregateRow(Map.of("opening_family", "Sicilian Defense"), 17));
    store.totals = new GameFeatureStore.AggregateTotals(59, 2);

    // limit == the number of groups returned, so the result could be truncated and the totals
    // query runs; see aggregate_underLimitDerivesTotalsWithoutSecondQuery for the other branch.
    AggregateResponse response =
        controller.aggregate(
            new AggregateRequest(
                "white.username = \"hikaru\" AND time.class = \"blitz\"",
                List.of("opening.family"),
                "count",
                2));

    assertThat(response.count()).isEqualTo(2);
    assertThat(response.groups().get(0).group())
        .containsEntry("opening_family", "Caro Kann Defense");
    assertThat(response.groups().get(0).count()).isEqualTo(42);
    assertThat(response.totalGames()).isEqualTo(59);
    assertThat(response.totalGroups()).isEqualTo(2);
    assertThat(response.truncated()).isFalse();

    // The store received the compiled aggregate with canonical group columns and the limit
    assertThat(store.lastGroupColumns).containsExactly("opening_family");
    assertThat(store.lastLimit).isEqualTo(2);
    assertThat(store.lastCompiled).isInstanceOf(CompiledQuery.class);
    CompiledQuery compiled = (CompiledQuery) store.lastCompiled;
    assertThat(compiled.selectSql())
        .contains("COUNT(*) AS group_count")
        .contains("GROUP BY opening_family");
    assertThat(compiled.parameters()).isEqualTo(List.of("hikaru", "blitz"));

    // The totals query reuses the same filter and grouping
    assertThat(store.lastTotalsCompiled).isInstanceOf(CompiledQuery.class);
    CompiledQuery totals = (CompiledQuery) store.lastTotalsCompiled;
    assertThat(totals.selectSql())
        .contains("COUNT(*) AS total_groups")
        .contains("COALESCE(SUM(group_count), 0) AS total_games")
        .contains("GROUP BY opening_family");
    assertThat(totals.parameters()).isEqualTo(List.of("hikaru", "blitz"));
  }

  /**
   * The totals query is a second COUNT-over-groups scan of the same corpus, and it can only tell
   * the caller something new when the group limit was actually reached. Under the limit the answer
   * is already in the returned rows, so the query is skipped — and must produce the same response
   * it would have produced.
   */
  @Test
  public void aggregate_underLimitDerivesTotalsWithoutSecondQuery() {
    store.rows =
        List.of(
            new AggregateRow(Map.of("opening_family", "Caro Kann Defense"), 42),
            new AggregateRow(Map.of("opening_family", "Sicilian Defense"), 17));
    // What the skipped query would have returned, so the two branches are directly comparable.
    store.totals = new GameFeatureStore.AggregateTotals(59, 2);

    AggregateResponse underLimit =
        controller.aggregate(
            new AggregateRequest("white.elo >= 1", List.of("opening_family"), "count", 20));

    assertThat(store.totalsCalls).isZero();
    assertThat(store.lastTotalsCompiled).isNull();

    AggregateResponse atLimit =
        controller.aggregate(
            new AggregateRequest("white.elo >= 1", List.of("opening_family"), "count", 2));

    assertThat(store.totalsCalls).isEqualTo(1);
    assertThat(store.lastTotalsCompiled).isNotNull();

    assertThat(underLimit.totalGames()).isEqualTo(atLimit.totalGames()).isEqualTo(59);
    assertThat(underLimit.totalGroups()).isEqualTo(atLimit.totalGroups()).isEqualTo(2);
    assertThat(underLimit.truncated()).isEqualTo(atLimit.truncated()).isFalse();
    assertThat(underLimit.count()).isEqualTo(atLimit.count()).isEqualTo(2);
  }

  @Test
  public void aggregate_reportsTruncationWhenTotalsExceedReturnedGroups() {
    store.rows = List.of(new AggregateRow(Map.of("opening_family", "Caro Kann Defense"), 103));
    store.totals = new GameFeatureStore.AggregateTotals(104, 2);

    AggregateResponse response =
        controller.aggregate(
            new AggregateRequest("white.elo >= 1", List.of("opening_family"), "count", 1));

    assertThat(response.count()).isEqualTo(1);
    assertThat(response.totalGames()).isEqualTo(104);
    assertThat(response.totalGroups()).isEqualTo(2);
    assertThat(response.truncated()).isTrue();
  }

  @Test
  public void aggregate_perspectiveGroupByUsesUnderscoreKeysAndAliasedCase() {
    store.rows =
        List.of(
            new AggregateRow(Map.of("me_color", "white", "outcome", "win"), 3),
            new AggregateRow(Map.of("me_color", "black", "outcome", "loss"), 2));
    store.totals = new GameFeatureStore.AggregateTotals(5, 2);

    AggregateResponse response =
        controller.aggregate(
            new AggregateRequest(
                "time.class = \"blitz\"", List.of("me.color", "outcome"), "count", 20, "hikaru"));

    assertThat(response.count()).isEqualTo(2);
    assertThat(response.groups().get(0).group()).containsEntry("me_color", "white");
    // Row-mapping keys are the underscore form of the perspective fields
    assertThat(store.lastGroupColumns).containsExactly("me_color", "outcome");
    CompiledQuery compiled = (CompiledQuery) store.lastCompiled;
    assertThat(compiled.selectSql())
        .contains("END) AS me_color")
        .contains("END) AS outcome")
        .contains("GROUP BY me_color, outcome");
    // SELECT CASE params (1 + 2), the outcome-metric block (10), participation guard (2), then
    // the filter value. SqlCompilerTest pins the metric SQL itself; what matters here is that a
    // player-scoped request reaches the store asking for the metric columns.
    assertThat(compiled.parameters()).hasSize(16).containsOnly("hikaru", "blitz");
    assertThat(store.lastOutcomeMetrics).isTrue();
  }

  @Test
  public void aggregate_perspectiveGroupByWithoutPlayerRejected() {
    assertThatThrownBy(
            () ->
                controller.aggregate(
                    new AggregateRequest("white.elo > 1", List.of("me.color"), null, 20)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("requires a player");
    assertThat(store.lastCompiled).isNull();
  }

  @Test
  public void aggregate_eloBucketGroupByCompilesThroughTheController() {
    // The REST groupBy string carries the bucket width, the compiled SQL floors the CASE to the
    // band's lower bound, and the response keys the numeric bound under the underscore name.
    store.rows =
        List.of(
            new AggregateRow(Collections.singletonMap("opponent_elo", 2400), 9),
            new AggregateRow(Collections.singletonMap("opponent_elo", null), 2));

    AggregateResponse response =
        controller.aggregate(
            new AggregateRequest(
                "time.class = \"bullet\"", List.of("opponent.elo(200)"), "count", 20, "hikaru"));

    assertThat(response.groups().get(0).group()).containsEntry("opponent_elo", 2400);
    assertThat(response.groups().get(1).group()).containsEntry("opponent_elo", null);
    assertThat(store.lastGroupColumns).containsExactly("opponent_elo");
    assertThat(((CompiledQuery) store.lastCompiled).selectSql())
        .contains("END) / 200 * 200 AS opponent_elo")
        .contains("GROUP BY opponent_elo");
  }

  @Test
  public void aggregate_invalidBucketWidthRejectedBeforeTheStore() {
    assertThatThrownBy(
            () ->
                controller.aggregate(
                    new AggregateRequest(
                        "white.elo > 1", List.of("me.elo(0)"), null, 20, "hikaru")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Bucket width must be a positive integer");
    assertThat(store.lastCompiled).isNull();
  }

  @Test
  public void aggregate_opponentTitleGroupByCompilesWithUnderscoreKey() {
    store.rows =
        List.of(
            new AggregateRow(Collections.singletonMap("opponent_title", "GM"), 9),
            new AggregateRow(Collections.singletonMap("opponent_title", null), 120));

    AggregateResponse response =
        controller.aggregate(
            new AggregateRequest(
                "time.class = \"bullet\"", List.of("opponent.title"), "count", 20, "hikaru"));

    assertThat(response.groups()).hasSize(2);
    assertThat(response.groups().get(0).group()).containsEntry("opponent_title", "GM");
    // Untitled opponents are a NULL group, serialized as a null value under the group key —
    // the same shape grouping the physical nullable title columns produces.
    assertThat(response.groups().get(1).group()).containsEntry("opponent_title", null);
    assertThat(store.lastGroupColumns).containsExactly("opponent_title");
    assertThat(((CompiledQuery) store.lastCompiled).selectSql())
        .contains("THEN black_title ELSE white_title END) AS opponent_title")
        .contains("GROUP BY opponent_title");
  }

  @Test
  public void aggregate_forwardsPlayerForPerspectiveFilters() {
    store.rows = List.of(new AggregateRow(Map.of("opening_family", "Caro Kann Defense"), 3));

    AggregateResponse response =
        controller.aggregate(
            new AggregateRequest(
                "outcome = \"win\"", List.of("opening_family"), "count", 20, "hikaru"));

    assertThat(response.count()).isEqualTo(1);
    CompiledQuery compiled = (CompiledQuery) store.lastCompiled;
    // The outcome-metric block (10), participation guard params, then the outcome CASE's two,
    // then the value
    assertThat(compiled.parameters()).hasSize(15).containsOnly("hikaru", "win");
  }

  @Test
  public void aggregate_perspectiveFilterWithoutPlayerRejected() {
    assertThatThrownBy(
            () ->
                controller.aggregate(
                    new AggregateRequest("outcome = \"win\"", List.of("opening_family"), null, 20)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("requires a player");
    assertThat(store.lastCompiled).isNull();
  }

  /**
   * The unscoped-player aggregate is refused before it reaches the store (#1313 item 14). The
   * request that matters is the natural one — "hikaru's most common openings" — which named a
   * player, used no perspective field, and counted the whole corpus under a heading that read as
   * hikaru's. Pinned here, at the endpoint, because that is where a caller meets it; the compiler
   * owns the rule so the MCP facade (which never touches this controller) is covered too.
   */
  @Test
  public void aggregate_playerThatWouldNotScopeTheResultIsRejectedBeforeStoreCall() {
    assertThatThrownBy(
            () ->
                controller.aggregate(
                    new AggregateRequest(
                        "num.moves >= 0", List.of("opening_family"), "count", 20, "hikaru")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("would not scope this aggregate");
    assertThat(store.lastCompiled)
        .as("a wrong answer must not be computed, let alone returned")
        .isNull();
  }

  /** The same request, scoped the two ways the rejection message offers, reaches the store. */
  @Test
  public void aggregate_scopedPlayerRequestsStillReachTheStore() {
    store.rows = List.of(new AggregateRow(Map.of("opening_family", "Sicilian Defense"), 3));
    store.totals = new GameFeatureStore.AggregateTotals(3, 1);

    controller.aggregate(
        new AggregateRequest(
            "white.username = \"hikaru\" OR black.username = \"hikaru\"",
            List.of("opening_family"),
            "count",
            20,
            "hikaru"));
    assertThat(store.lastCompiled).as("explicit username filter").isNotNull();

    store.lastCompiled = null;
    controller.aggregate(
        new AggregateRequest("num.moves >= 0", List.of("me.color"), "count", 20, "hikaru"));
    assertThat(store.lastCompiled).as("perspective field in groupBy").isNotNull();
  }

  /**
   * A corpus-wide aggregate asks the store for counts only. The positive twin is
   * aggregate_perspectiveGroupByUsesUnderscoreKeysAndAliasedCase, which asserts the same flag true
   * on a player-scoped request — one request shape decides both the SQL and the row mapping, so a
   * mismatch here is a per-row failure in the driver rather than a wrong number.
   */
  @Test
  public void aggregate_withoutAPlayerAsksTheStoreForCountsOnly() {
    store.rows = List.of(new AggregateRow(Map.of("opening_family", "Caro Kann Defense"), 3));

    controller.aggregate(
        new AggregateRequest("white.elo >= 1", List.of("opening_family"), "count", 20));

    assertThat(store.lastOutcomeMetrics).isFalse();
    assertThat(((CompiledQuery) store.lastCompiled).selectSql()).doesNotContain("AS wins");
  }

  /** orderBy reaches the compiler rather than being validated and dropped. */
  @Test
  public void aggregate_orderByScoreRanksBeforeTheLimit() {
    store.rows = List.of(new AggregateRow(Map.of("opening_family", "Sicilian Defense"), 3));

    controller.aggregate(
        new AggregateRequest(
            "white.username = \"hikaru\" OR black.username = \"hikaru\"",
            List.of("opening_family"),
            "score",
            20,
            "hikaru"));

    assertThat(((CompiledQuery) store.lastCompiled).selectSql())
        .contains(") agg ORDER BY score_points * 1.0 / group_count DESC");
    assertThat(store.lastOutcomeMetrics).isTrue();
  }

  /** The floor reaches both queries; a totals query without it would count excluded groups. */
  @Test
  public void aggregate_minGamesFloorsBothTheGroupsAndTheTotalsQuery() {
    store.rows = List.of(new AggregateRow(Map.of("opening_family", "Sicilian Defense"), 3));
    store.totals = new GameFeatureStore.AggregateTotals(3, 1);

    controller.aggregate(
        new AggregateRequest("white.elo >= 1", List.of("opening_family"), "count", 1, null, 5));

    assertThat(((CompiledQuery) store.lastCompiled).selectSql()).contains("HAVING COUNT(*) >= ?");
    assertThat(((CompiledQuery) store.lastCompiled).parameters()).containsExactly(1, 5);
    assertThat(((CompiledQuery) store.lastTotalsCompiled).selectSql())
        .contains("HAVING COUNT(*) >= ?");
    assertThat(((CompiledQuery) store.lastTotalsCompiled).parameters()).containsExactly(1, 5);
  }

  @Test
  public void aggregate_unsupportedOrderByRejectedBeforeStoreCall() {
    assertThatThrownBy(
            () ->
                controller.aggregate(
                    new AggregateRequest("white.elo > 1", List.of("eco"), "elo", 20)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("orderBy");
    assertThat(store.lastCompiled).isNull();
  }

  /**
   * Ranking by score names a column only a player-scoped query carries, so the endpoint refuses the
   * combination rather than letting the database refuse it as a syntax error the caller cannot act
   * on.
   */
  @Test
  public void aggregate_orderByScoreWithoutPlayerRejectedBeforeStoreCall() {
    assertThatThrownBy(
            () ->
                controller.aggregate(
                    new AggregateRequest("white.elo > 1", List.of("eco"), "score", 20)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("requires a player");
    assertThat(store.lastCompiled).isNull();
  }

  @Test
  public void aggregate_invalidRequestRejectedBeforeStoreCall() {
    assertThatThrownBy(
            () -> controller.aggregate(new AggregateRequest("white.elo > 1", List.of(), null, 20)))
        .isInstanceOf(IllegalArgumentException.class);
    assertThat(store.lastCompiled).isNull();
  }

  @Test
  public void aggregate_unknownGroupByFieldRejected() {
    assertThatThrownBy(
            () ->
                controller.aggregate(
                    new AggregateRequest("white.elo > 1", List.of("pgn"), null, 20)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unknown field");
    assertThat(store.lastCompiled).isNull();
  }

  private static final class RecordingStore implements GameFeatureStore {

    /** Not part of the AggregateController surface: only the worker flushes. */
    @Override
    public boolean flushOwned(
        java.util.UUID requestId,
        String ownerId,
        Instant now,
        List<GameFeature> features,
        Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame) {
      throw new UnsupportedOperationException("AggregateController tests never flush");
    }

    List<AggregateRow> rows = List.of();
    AggregateTotals totals = new AggregateTotals(0, 0);
    Object lastCompiled;
    Object lastTotalsCompiled;
    List<String> lastGroupColumns;
    int lastLimit;
    Boolean lastOutcomeMetrics;
    int totalsCalls;

    @Override
    public List<AggregateRow> aggregate(
        Object compiledQuery, List<String> groupColumns, boolean withOutcomeMetrics, int limit) {
      this.lastCompiled = compiledQuery;
      this.lastGroupColumns = groupColumns;
      this.lastOutcomeMetrics = withOutcomeMetrics;
      this.lastLimit = limit;
      return rows;
    }

    @Override
    public AggregateTotals aggregateTotals(Object compiledQuery) {
      this.lastTotalsCompiled = compiledQuery;
      this.totalsCalls++;
      return totals;
    }

    @Override
    public void insertBatch(List<GameFeature> features) {}

    @Override
    public int deleteOlderThan(Instant threshold) {
      return 0;
    }

    @Override
    public void insertOccurrencesBatch(
        Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame) {}

    @Override
    public void deleteOccurrencesByGameUrls(List<String> gameUrls) {}

    @Override
    public List<GameFeature> query(Object compiledQuery, int limit, int offset) {
      return List.of();
    }

    @Override
    public Map<String, Map<String, List<OccurrenceRow>>> queryOccurrences(List<String> gameUrls) {
      return Map.of();
    }

    @Override
    public List<GameForReanalysis> fetchForReanalysis(int limit, int offset) {
      return List.of();
    }
  }
}
