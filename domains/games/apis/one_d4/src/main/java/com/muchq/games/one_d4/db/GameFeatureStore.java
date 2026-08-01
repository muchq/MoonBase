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

  /**
   * Replaces the occurrences for a set of games as one unit. For callers that recompute motifs for
   * games without holding a request lease — reanalysis — and therefore have no token to present.
   *
   * <p>{@code gameUrls} is the full set to clear, which is not the same as the map's key set: a
   * game whose reanalysis found no motifs must still lose the occurrences it had.
   *
   * <p>The default composes the two primitives and is <em>not</em> atomic. It exists so test fakes
   * need not reimplement a transaction they do not have; any store backed by a real database must
   * override it, because the whole hazard is that a delete committing separately from its insert
   * lets a concurrent writer's rows survive alongside the new ones.
   */
  default void replaceOccurrences(
      List<String> gameUrls,
      Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame) {
    deleteOccurrencesByGameUrls(gameUrls);
    insertOccurrencesBatch(occurrencesByGame);
  }

  int deleteOlderThan(Instant threshold);

  void insertOccurrencesBatch(
      Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame);

  void deleteOccurrencesByGameUrls(List<String> gameUrls);

  List<GameFeature> query(Object compiledQuery, int limit, int offset);

  /**
   * Runs a compiled aggregate query (see SqlCompiler.compileAggregate) and maps each group row
   * using the given canonical group column names.
   */
  List<AggregateRow> aggregate(Object compiledQuery, List<String> groupColumns, int limit);

  /**
   * Runs a compiled aggregate totals query (see SqlCompiler.compileAggregateTotals) and returns the
   * untruncated group/game counts behind an aggregate, so callers can report truncation.
   */
  AggregateTotals aggregateTotals(Object compiledQuery);

  Map<String, Map<String, List<OccurrenceRow>>> queryOccurrences(List<String> gameUrls);

  /** Returns a batch of game records (requestId, gameUrl, pgn) for re-analysis. */
  List<GameForReanalysis> fetchForReanalysis(int limit, int offset);

  record GameForReanalysis(UUID requestId, String gameUrl, String pgn) {}

  /** Untruncated totals for an aggregate: all games matching the filter, and all groups. */
  record AggregateTotals(long totalGames, long totalGroups) {}
}
