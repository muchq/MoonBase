package com.muchq.games.one_d4.testing;

import com.muchq.games.one_d4.api.dto.AggregateRow;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.time.Instant;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.jspecify.annotations.Nullable;

/**
 * The one {@link GameFeatureStore} double (#1534), covering the three surfaces one_d4's tests ask
 * of it: reads, aggregates, and the openings re-derive.
 *
 * <p>Reads and aggregates are programmed rather than computed, because both take a compiled query
 * this cannot execute — a fake that tried would be reimplementing the SQL compiler. What it can do
 * is record what it was asked, which is the half that catches a caller passing the wrong limit or
 * the wrong compiled query with every assertion still green. Openings are stored for real: that
 * surface takes no compiled query, so there is nothing to stand in for.
 *
 * <p>Reads and aggregates record into separate slots. Shared, an aggregate assertion would pass on
 * an argument a read recorded.
 */
public final class FakeGameFeatureStore implements GameFeatureStore {

  private List<GameFeature> queryResult = List.of();
  private Map<String, Map<String, List<OccurrenceRow>>> occurrences = Map.of();
  private List<AggregateRow> aggregateResult = List.of();
  private AggregateTotals aggregateTotals = new AggregateTotals(0, 0);
  private @Nullable RuntimeException queryFailure;
  private Runnable onQuery = () -> {};

  private int queryCount;
  private @Nullable Object lastQueryCompiled;
  private int lastQueryLimit = -1;
  private int lastQueryOffset = -1;

  private @Nullable Object lastAggregateCompiled;
  private @Nullable List<String> lastAggregateGroupColumns;
  private @Nullable Boolean lastAggregateOutcomeMetrics;
  private int lastAggregateLimit = -1;
  private @Nullable Object lastTotalsCompiled;
  private int totalsCalls;

  private final List<GameOpening> openings = new ArrayList<>();
  private final List<String> writtenUrls = new ArrayList<>();

  public void setQueryResult(List<GameFeature> result) {
    this.queryResult = result;
  }

  public void setOccurrences(@Nullable Map<String, Map<String, List<OccurrenceRow>>> result) {
    this.occurrences = result == null ? Map.of() : result;
  }

  public void failQueriesWith(RuntimeException failure) {
    this.queryFailure = failure;
  }

  /** Runs inside query(), before it returns — lets a test simulate things happening mid-load. */
  public void onQuery(Runnable hook) {
    this.onQuery = hook;
  }

  public void setAggregateRows(List<AggregateRow> rows) {
    this.aggregateResult = rows;
  }

  public void setAggregateTotals(AggregateTotals totals) {
    this.aggregateTotals = totals;
  }

  /**
   * Forgets what was asked, keeping what was programmed — for a test that makes a second call and
   * needs to show the recorder answers for that one rather than the first.
   */
  public void clearRecordedCalls() {
    queryCount = 0;
    lastQueryCompiled = null;
    lastQueryLimit = -1;
    lastQueryOffset = -1;
    lastAggregateCompiled = null;
    lastAggregateGroupColumns = null;
    lastAggregateOutcomeMetrics = null;
    lastAggregateLimit = -1;
    lastTotalsCompiled = null;
    totalsCalls = 0;
  }

  public int queryCount() {
    return queryCount;
  }

  public @Nullable Object lastQueryCompiled() {
    return lastQueryCompiled;
  }

  public int lastQueryLimit() {
    return lastQueryLimit;
  }

  public int lastQueryOffset() {
    return lastQueryOffset;
  }

  public @Nullable Object lastAggregateCompiled() {
    return lastAggregateCompiled;
  }

  public @Nullable List<String> lastAggregateGroupColumns() {
    return lastAggregateGroupColumns;
  }

  public @Nullable Boolean lastAggregateOutcomeMetrics() {
    return lastAggregateOutcomeMetrics;
  }

  public int lastAggregateLimit() {
    return lastAggregateLimit;
  }

  public @Nullable Object lastTotalsCompiled() {
    return lastTotalsCompiled;
  }

  public int totalsCalls() {
    return totalsCalls;
  }

  /** Seeds a game for the openings re-derive to find. */
  public void addOpening(String gameUrl, @Nullable String name, @Nullable String family) {
    openings.add(new GameOpening(gameUrl, name, family));
  }

  public @Nullable String familyOf(String gameUrl) {
    return openings.stream()
        .filter(o -> o.gameUrl().equals(gameUrl))
        .findFirst()
        .orElseThrow(() -> new AssertionError("no opening stored for " + gameUrl))
        .openingFamily();
  }

  /** Every url written, in order. A paging bug shows up here as a duplicate. */
  public List<String> writtenUrls() {
    return List.copyOf(writtenUrls);
  }

  // The arguments are recorded before the programmed failure is thrown, so a test asserting that
  // a failed load still counted as an attempt can read the counter.
  @Override
  public List<GameFeature> query(Object compiledQuery, int limit, int offset) {
    queryCount++;
    lastQueryCompiled = compiledQuery;
    lastQueryLimit = limit;
    lastQueryOffset = offset;
    if (queryFailure != null) {
      throw queryFailure;
    }
    onQuery.run();
    return queryResult;
  }

  /**
   * Only the urls that have occurrences, as the DAO returns — it groups the rows it found, so a
   * game with none gets no key at all. Callers read it with {@code getOrDefault}; one that switched
   * to {@code get} would NPE in production, and a fake answering an empty map per requested url
   * would keep it green.
   */
  @Override
  public Map<String, Map<String, List<OccurrenceRow>>> queryOccurrences(List<String> gameUrls) {
    Map<String, Map<String, List<OccurrenceRow>>> found = new LinkedHashMap<>();
    for (String url : gameUrls) {
      Map<String, List<OccurrenceRow>> forUrl = occurrences.get(url);
      if (forUrl != null && !forUrl.isEmpty()) {
        found.put(url, forUrl);
      }
    }
    return found;
  }

  @Override
  public List<AggregateRow> aggregate(
      Object compiledQuery, List<String> groupColumns, boolean withOutcomeMetrics, int limit) {
    lastAggregateCompiled = compiledQuery;
    lastAggregateGroupColumns = groupColumns;
    lastAggregateOutcomeMetrics = withOutcomeMetrics;
    lastAggregateLimit = limit;
    return aggregateResult;
  }

  @Override
  public AggregateTotals aggregateTotals(Object compiledQuery) {
    lastTotalsCompiled = compiledQuery;
    totalsCalls++;
    return aggregateTotals;
  }

  /** Ordered and stable, like the (indexed_at, game_url) cursor this stands in for. */
  @Override
  public List<GameOpening> fetchOpeningsForRederive(int limit, int offset) {
    int start = Math.min(offset, openings.size());
    int end = Math.min(offset + limit, openings.size());
    return List.copyOf(openings.subList(start, end));
  }

  // Writes only the family, like the UPDATE it stands in for. Rewriting the name here would hide
  // a caller deriving a family from a name the row no longer holds.
  @Override
  public int updateOpeningFamilies(List<GameOpening> updates) {
    for (GameOpening update : updates) {
      writtenUrls.add(update.gameUrl());
      openings.replaceAll(
          stored ->
              stored.gameUrl().equals(update.gameUrl())
                  ? new GameOpening(stored.gameUrl(), stored.openingName(), update.openingFamily())
                  : stored);
    }
    return updates.size();
  }

  /** Only the worker flushes. A read-side test reaching this is asserting the wrong thing. */
  @Override
  public boolean flushOwned(
      UUID requestId,
      String ownerId,
      Instant now,
      List<GameFeature> features,
      Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame) {
    throw new UnsupportedOperationException("the read side never flushes");
  }

  /**
   * Retention is the worker's, like the flush. A read-side test reaching these is asserting the
   * wrong thing; answering 0 would read as a pass.
   */
  @Override
  public void insertBatch(List<GameFeature> features) {
    throw new UnsupportedOperationException("the read side never inserts");
  }

  @Override
  public int deleteOlderThan(Instant threshold) {
    throw new UnsupportedOperationException("the read side never sweeps");
  }
}
