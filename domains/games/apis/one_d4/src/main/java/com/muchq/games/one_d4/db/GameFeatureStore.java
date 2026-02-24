package com.muchq.games.one_d4.db;

import com.muchq.games.one_d4.api.dto.GameFeature;
import java.time.Instant;
import java.util.List;

public interface GameFeatureStore {
  void insert(GameFeature feature);

  void deleteOlderThan(Instant threshold);

  List<GameFeature> query(Object compiledQuery, int limit, int offset);
}
