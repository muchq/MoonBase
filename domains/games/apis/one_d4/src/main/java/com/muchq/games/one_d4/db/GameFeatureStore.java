package com.muchq.games.one_d4.db;

import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.time.Instant;
import java.util.List;
import java.util.Map;

public interface GameFeatureStore {
  void insert(GameFeature feature);

  void deleteOlderThan(Instant threshold);

  void insertOccurrences(
      String gameUrl, Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences);

  List<GameFeature> query(Object compiledQuery, int limit, int offset);

  Map<String, Map<String, List<OccurrenceRow>>> queryOccurrences(List<String> gameUrls);
}
