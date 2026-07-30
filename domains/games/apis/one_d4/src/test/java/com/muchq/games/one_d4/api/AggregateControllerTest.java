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

    AggregateResponse response =
        controller.aggregate(
            new AggregateRequest(
                "white.username = \"hikaru\" AND time.class = \"blitz\"",
                List.of("opening.family"),
                "count",
                20));

    assertThat(response.count()).isEqualTo(2);
    assertThat(response.groups().get(0).group())
        .containsEntry("opening_family", "Caro Kann Defense");
    assertThat(response.groups().get(0).count()).isEqualTo(42);
    assertThat(response.totalGames()).isEqualTo(59);
    assertThat(response.totalGroups()).isEqualTo(2);
    assertThat(response.truncated()).isFalse();

    // The store received the compiled aggregate with canonical group columns and the limit
    assertThat(store.lastGroupColumns).containsExactly("opening_family");
    assertThat(store.lastLimit).isEqualTo(20);
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
    // SELECT CASE params (1 + 2), participation guard (2), then the filter value
    assertThat(compiled.parameters())
        .isEqualTo(List.of("hikaru", "hikaru", "hikaru", "hikaru", "hikaru", "blitz"));
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
  public void aggregate_otherPerspectiveFieldsStillRejectedInGroupBy() {
    assertThatThrownBy(
            () ->
                controller.aggregate(
                    new AggregateRequest("white.elo > 1", List.of("me.elo"), null, 20, "hikaru")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Perspective fields are not supported in groupBy");
    assertThat(store.lastCompiled).isNull();
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
    // Participation guard params first, then the outcome CASE's two, then the value
    assertThat(compiled.parameters())
        .isEqualTo(List.of("hikaru", "hikaru", "hikaru", "hikaru", "win"));
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
    List<AggregateRow> rows = List.of();
    AggregateTotals totals = new AggregateTotals(0, 0);
    Object lastCompiled;
    Object lastTotalsCompiled;
    List<String> lastGroupColumns;
    int lastLimit;

    @Override
    public List<AggregateRow> aggregate(
        Object compiledQuery, List<String> groupColumns, int limit) {
      this.lastCompiled = compiledQuery;
      this.lastGroupColumns = groupColumns;
      this.lastLimit = limit;
      return rows;
    }

    @Override
    public AggregateTotals aggregateTotals(Object compiledQuery) {
      this.lastTotalsCompiled = compiledQuery;
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
