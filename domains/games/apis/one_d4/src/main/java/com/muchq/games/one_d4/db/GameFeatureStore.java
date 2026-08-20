package com.muchq.games.one_d4.db;

import com.muchq.games.one_d4.api.dto.AggregateRow;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.jspecify.annotations.Nullable;

public interface GameFeatureStore {
  void insertBatch(List<GameFeature> features);

  /**
   * Writes a batch of games and their occurrences as one unit, only if {@code ownerId} still holds
   * the lease on {@code requestId}. Returns false without writing anything if it does not — which
   * includes the case where there is nothing to write, so a caller can use an empty batch purely to
   * ask whether it still owns the request.
   *
   * <p>This replaces the three separate calls a flush used to make, and the single transaction is
   * the point rather than a tidy-up. Occurrences are written by deleting a game's existing rows and
   * inserting the new ones, and {@code motif_occurrences} has no uniqueness beyond a random UUID
   * primary key. Split across transactions, two writers over the same game interleave as delete,
   * delete, insert, insert and both sets survive — every motif for that game then reads double.
   * That is reproducible in a test, not a worry; the delete and the insert have to be indivisible.
   *
   * <p>Two live requests can legitimately cover the same game: a game has two players, so indexing
   * each of them reaches it. Dedupe cannot prevent that overlap because the requests are for
   * different ranges, which is why the atomicity has to live here rather than in a lock on the
   * request row.
   *
   * <p>The ownership check is the second, separate guarantee — it stops a worker that has lost its
   * lease from continuing to write against a range someone else now owns. It has to take a row
   * lock, not merely run inside the same transaction: nothing sets an isolation level, so a plain
   * read is a snapshot that a concurrent takeover can invalidate before this transaction commits.
   */
  boolean flushOwned(
      UUID requestId,
      String ownerId,
      Instant now,
      List<GameFeature> features,
      Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame);

  int deleteOlderThan(Instant threshold);

  List<GameFeature> query(Object compiledQuery, int limit, int offset);

  /**
   * Runs a compiled aggregate query (see SqlCompiler.compileAggregate) and maps each group row
   * using the given canonical group column names.
   *
   * <p>{@code withOutcomeMetrics} must match what the query was compiled to SELECT — {@code
   * AggregateSpec.hasOutcomeMetrics()} is the one answer both sides take it from. A row mapper that
   * read the metric columns from a query compiled without them would fail per row, at the database
   * driver, for what is really a caller mistake.
   */
  List<AggregateRow> aggregate(
      Object compiledQuery, List<String> groupColumns, boolean withOutcomeMetrics, int limit);

  /** Counts only, for aggregates compiled with no perspective player. */
  default List<AggregateRow> aggregate(Object compiledQuery, List<String> groupColumns, int limit) {
    return aggregate(compiledQuery, groupColumns, false, limit);
  }

  /**
   * Runs a compiled aggregate totals query (see SqlCompiler.compileAggregateTotals) and returns the
   * untruncated group/game counts behind an aggregate, so callers can report truncation.
   */
  AggregateTotals aggregateTotals(Object compiledQuery);

  Map<String, Map<String, List<OccurrenceRow>>> queryOccurrences(List<String> gameUrls);

  /**
   * Returns a batch of stored opening values, for re-deriving {@code opening_family} from {@code
   * opening_name} without refetching anything (#1350).
   *
   * <p>Ordered by {@code (indexed_at, game_url)}, neither of which this pass writes, so a cursor
   * cannot shift under a row that has already been rewritten.
   */
  List<GameOpening> fetchOpeningsForRederive(int limit, int offset);

  /**
   * Writes {@code opening_family} for the given games, and returns how many rows changed.
   *
   * <p>Only the family is written, and only where the row still holds the {@code opening_name} the
   * caller derived from — an indexer upsert rewrites name and family together, so an unconditional
   * write could land a family derived from a name the row no longer has.
   *
   * <p>The other enriched columns stay on the reindex path: titles come from player profiles at
   * index time, and while the ECOUrl behind {@code opening_name} is not a column, the stored PGN
   * usually carries its {@code [ECOUrl "..."]} tag — so a local name re-derive is possible and
   * simply not built here.
   */
  int updateOpeningFamilies(List<GameOpening> updates);

  /**
   * A game's stored opening values. Both are nullable: chess.com does not always give an ECOUrl to
   * derive a name from, and a name that is nothing but a move continuation has no family.
   */
  record GameOpening(
      String gameUrl, @Nullable String openingName, @Nullable String openingFamily) {}

  /** Untruncated totals for an aggregate: all games matching the filter, and all groups. */
  record AggregateTotals(long totalGames, long totalGroups) {}
}
