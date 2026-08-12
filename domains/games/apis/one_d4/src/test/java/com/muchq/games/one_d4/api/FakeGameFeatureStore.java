package com.muchq.games.one_d4.api;

import com.muchq.games.one_d4.api.dto.AggregateRow;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.time.Instant;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

/** Programmable read-side store for query/cache tests. Write-side methods are unsupported. */
final class FakeGameFeatureStore implements GameFeatureStore {

  private List<GameFeature> queryResult = List.of();
  private Map<String, Map<String, List<OccurrenceRow>>> occurrencesResult = Map.of();
  private int queryCount;
  private Object lastCompiled;
  private int lastLimit = -1;
  private int lastOffset = -1;
  private RuntimeException queryFailure;
  private Runnable onQuery = () -> {};

  void setQueryResult(List<GameFeature> result) {
    this.queryResult = result;
  }

  void setOccurrencesResult(Map<String, Map<String, List<OccurrenceRow>>> result) {
    this.occurrencesResult = result == null ? Map.of() : result;
  }

  void failQueriesWith(RuntimeException failure) {
    this.queryFailure = failure;
  }

  /** Runs inside query(), before it returns — lets a test simulate things happening mid-load. */
  void onQuery(Runnable hook) {
    this.onQuery = hook;
  }

  int queryCount() {
    return queryCount;
  }

  Object lastCompiled() {
    return lastCompiled;
  }

  int lastLimit() {
    return lastLimit;
  }

  int lastOffset() {
    return lastOffset;
  }

  // Records its arguments so tests can pin what was actually asked of the store — a fake that
  // discards them lets a caller pass the wrong compiled query or limit/offset (or warm the
  // wrong request) with every assertion still green.
  @Override
  public List<GameFeature> query(Object compiledQuery, int limit, int offset) {
    queryCount++;
    lastCompiled = compiledQuery;
    lastLimit = limit;
    lastOffset = offset;
    if (queryFailure != null) {
      throw queryFailure;
    }
    onQuery.run();
    return queryResult;
  }

  @Override
  public Map<String, Map<String, List<OccurrenceRow>>> queryOccurrences(List<String> gameUrls) {
    if (gameUrls.isEmpty()) return Map.of();
    Map<String, Map<String, List<OccurrenceRow>>> out = new LinkedHashMap<>();
    for (String url : gameUrls) {
      out.put(url, occurrencesResult.getOrDefault(url, Map.of()));
    }
    return out;
  }

  /** Not part of the query surface: only the worker flushes. */
  @Override
  public boolean flushOwned(
      UUID requestId,
      String ownerId,
      Instant now,
      List<GameFeature> features,
      Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame) {
    throw new UnsupportedOperationException("query tests never flush");
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
  public List<AggregateRow> aggregate(
      Object compiledQuery, List<String> groupColumns, boolean withOutcomeMetrics, int limit) {
    return List.of();
  }

  @Override
  public AggregateTotals aggregateTotals(Object compiledQuery) {
    return new AggregateTotals(0, 0);
  }

  @Override
  public List<GameForReanalysis> fetchForReanalysis(int limit, int offset) {
    return List.of();
  }
}
