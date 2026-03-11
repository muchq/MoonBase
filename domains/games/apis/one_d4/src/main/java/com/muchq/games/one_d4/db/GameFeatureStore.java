package com.muchq.games.one_d4.db;

import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;

public interface GameFeatureStore {
  /** Fetches a single game by URL with full PGN, for detail view. */
  Optional<GameFeature> findByGameUrl(String gameUrl);
  void insertBatch(List<GameFeature> features);

  int deleteOlderThan(Instant threshold);

  void insertOccurrencesBatch(
      Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame);

  void deleteOccurrencesByGameUrls(List<String> gameUrls);

  default List<GameFeature> query(Object compiledQuery, int limit, int offset) {
    return query(compiledQuery, limit, offset, true);
  }

  List<GameFeature> query(Object compiledQuery, int limit, int offset, boolean includePgn);

  int count(Object compiledQuery);

  Map<String, Map<String, List<OccurrenceRow>>> queryOccurrences(List<String> gameUrls);

  /** Returns a batch of game records (requestId, gameUrl, pgn) for re-analysis. */
  List<GameForReanalysis> fetchForReanalysis(int limit, int offset);

  record GameForReanalysis(UUID requestId, String gameUrl, String pgn) {}
}
