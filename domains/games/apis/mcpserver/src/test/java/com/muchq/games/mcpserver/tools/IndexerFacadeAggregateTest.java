package com.muchq.games.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chessql.compiler.SqlCompiler;
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

/**
 * The aggregate totals are a second COUNT-over-groups scan of the whole matching corpus, so they
 * are worth paying for only when the group limit could have cut something off. {@link
 * IndexerToolsTest} covers what the totals mean against real H2; this pins how many queries it
 * takes to get them, which a store backed by a real database cannot show.
 */
public class IndexerFacadeAggregateTest {

  private static final List<AggregateRow> TWO_GROUPS =
      List.of(
          new AggregateRow(Map.of("opening_family", "Caro Kann Defense"), 42),
          new AggregateRow(Map.of("opening_family", "Sicilian Defense"), 17));

  private final CountingStore store = new CountingStore();
  private final IndexerFacade facade = new IndexerFacade(null, store, null, new SqlCompiler());

  @Test
  public void aggregate_underLimitDerivesTotalsWithoutSecondQuery() {
    store.rows = TWO_GROUPS;
    // What the skipped query would have returned, so the two branches are directly comparable.
    store.totals = new GameFeatureStore.AggregateTotals(59, 2);

    IndexerFacade.AggregateResult underLimit =
        facade.aggregate("white.elo >= 1", List.of("opening_family"), null, 20);

    assertThat(store.totalsCalls).isZero();

    IndexerFacade.AggregateResult atLimit =
        facade.aggregate("white.elo >= 1", List.of("opening_family"), null, 2);

    assertThat(store.totalsCalls).isEqualTo(1);

    assertThat(underLimit.totalGames()).isEqualTo(atLimit.totalGames()).isEqualTo(59);
    assertThat(underLimit.totalGroups()).isEqualTo(atLimit.totalGroups()).isEqualTo(2);
    assertThat(underLimit.groups()).isEqualTo(atLimit.groups());
  }

  /** No groups at all is the extreme of the fast path: zero rows, zero games, still no query. */
  @Test
  public void aggregate_emptyResultDerivesZeroTotalsWithoutSecondQuery() {
    store.rows = List.of();
    store.totals = new GameFeatureStore.AggregateTotals(999, 999);

    IndexerFacade.AggregateResult result =
        facade.aggregate("white.elo >= 1", List.of("opening_family"), null, 20);

    assertThat(store.totalsCalls).isZero();
    assertThat(result.totalGames()).isZero();
    assertThat(result.totalGroups()).isZero();
  }

  private static final class CountingStore implements GameFeatureStore {
    List<AggregateRow> rows = List.of();
    AggregateTotals totals = new AggregateTotals(0, 0);
    int totalsCalls;

    @Override
    public List<AggregateRow> aggregate(
        Object compiledQuery, List<String> groupColumns, int limit) {
      return rows.size() > limit ? rows.subList(0, limit) : rows;
    }

    @Override
    public AggregateTotals aggregateTotals(Object compiledQuery) {
      totalsCalls++;
      return totals;
    }

    @Override
    public void insertBatch(List<GameFeature> features) {}

    /** Not part of the aggregate surface: only the indexer's worker flushes. */
    @Override
    public boolean flushOwned(
        java.util.UUID requestId,
        String ownerId,
        Instant now,
        List<GameFeature> features,
        Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame) {
      throw new UnsupportedOperationException("aggregate tests never flush");
    }

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
