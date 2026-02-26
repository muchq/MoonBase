package com.muchq.games.one_d4.db;

import com.muchq.games.one_d4.api.dto.AttackOccurrenceRow;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.engine.model.AttackOccurrence;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.UUID;

public interface GameFeatureStore {
  void insert(GameFeature feature);

  int deleteOlderThan(Instant threshold);

  void insertOccurrences(
      String gameUrl, Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences);

  void insertAttackOccurrences(String gameUrl, List<AttackOccurrence> attacks);

  void deleteOccurrencesByGameUrl(String gameUrl);

  void deleteAttacksByGameUrl(String gameUrl);

  List<GameFeature> query(Object compiledQuery, int limit, int offset);

  Map<String, Map<String, List<OccurrenceRow>>> queryOccurrences(List<String> gameUrls);

  Map<String, List<AttackOccurrenceRow>> queryAttackOccurrences(List<String> gameUrls);

  /** Returns a batch of game records (requestId, gameUrl, pgn) for re-analysis. */
  List<GameForReanalysis> fetchForReanalysis(int limit, int offset);

  /** Updates all motif boolean columns for the given game URL. */
  void updateMotifs(String gameUrl, GameFeatures features);

  record GameForReanalysis(UUID requestId, String gameUrl, String pgn) {}
}
