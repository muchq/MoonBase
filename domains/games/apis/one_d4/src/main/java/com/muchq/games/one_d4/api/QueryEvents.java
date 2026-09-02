package com.muchq.games.one_d4.api;

import com.muchq.platform.yodel.CustomMetrics;
import io.micronaut.core.annotation.Nullable;
import jakarta.inject.Singleton;
import java.util.List;
import java.util.Map;
import java.util.function.Function;

/**
 * Runs a query or aggregate handler inside a {@link QueryEvent}: the event is finished exactly once
 * on every way out — normal return as {@code ok}, an exception or error with the outcome {@link
 * QueryEvent#outcomeOf} assigns, rethrown untouched so the error handler still answers.
 *
 * <p>Also owns the two instruments the events feed. Their bounded label sets are declared at
 * construction so that the first request after a deploy is counted rather than becoming the
 * baseline the dashboard's rate is computed against.
 */
@Singleton
public class QueryEvents {
  /** Queries by entry, source, outcome, and cache; exported as {@code one_d4_queries_total}. */
  static final String QUERIES = "one_d4_queries";

  /** Handler wall time by entry and source, in microseconds. */
  static final String DURATION = "one_d4_query_duration_micros";

  /** One millisecond to ten seconds: a snapshot hit sits in the first bucket, a cold scan high. */
  static final double[] DURATION_BOUNDS = {
    1_000,
    2_500,
    5_000,
    10_000,
    25_000,
    50_000,
    100_000,
    250_000,
    500_000,
    1_000_000,
    2_500_000,
    5_000_000,
    10_000_000
  };

  static final List<String> SOURCES =
      List.of(QueryEvent.SOURCE_MCP, QueryEvent.SOURCE_UI, QueryEvent.SOURCE_API);
  static final List<String> OUTCOMES =
      List.of(QueryEvent.OUTCOME_OK, QueryEvent.OUTCOME_INVALID, QueryEvent.OUTCOME_FAILED);

  private final CustomMetrics metrics;

  public QueryEvents(CustomMetrics metrics) {
    this.metrics = metrics;
    metrics.defineDistribution(DURATION, DURATION_BOUNDS);
    for (String source : SOURCES) {
      for (String entry : List.of(QueryEvent.ENTRY_QUERY, QueryEvent.ENTRY_AGGREGATE)) {
        metrics.defineDistributionSeries(DURATION, Map.of("entry", entry, "source", source));
        for (String outcome : OUTCOMES) {
          for (String cache : cachesFor(entry)) {
            metrics.defineCounter(QUERIES, labels(entry, source, outcome, cache));
          }
        }
      }
    }
  }

  /** A query is served from the snapshot or live, or fails before deciding; an aggregate never. */
  private static List<String> cachesFor(String entry) {
    if (entry.equals(QueryEvent.ENTRY_QUERY)) {
      return List.of(QueryEvent.CACHE_SNAPSHOT, QueryEvent.CACHE_LIVE, QueryEvent.CACHE_NONE);
    }
    return List.of(QueryEvent.CACHE_NONE);
  }

  static Map<String, String> labels(String entry, String source, String outcome, String cache) {
    return Map.of("entry", entry, "source", source, "outcome", outcome, "cache", cache);
  }

  <T> T observe(
      String entry,
      @Nullable String userAgent,
      @Nullable String origin,
      Function<QueryEvent, T> handler) {
    QueryEvent event = new QueryEvent(this, entry, userAgent, origin);
    T result;
    try {
      result = handler.apply(event);
    } catch (RuntimeException | Error e) {
      event.finish(QueryEvent.outcomeOf(e));
      throw e;
    }
    event.finish(QueryEvent.OUTCOME_OK);
    return result;
  }

  void record(String entry, String source, String outcome, String cache, long durationUs) {
    metrics.increment(QUERIES, labels(entry, source, outcome, cache));
    metrics.record(DURATION, durationUs, Map.of("entry", entry, "source", source));
  }
}
