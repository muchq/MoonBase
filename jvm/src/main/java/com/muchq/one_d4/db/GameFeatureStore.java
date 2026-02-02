package com.muchq.one_d4.db;

import java.time.Instant;
import java.util.List;
import java.util.UUID;

public interface GameFeatureStore {
    void insert(GameFeature feature);
    List<GameFeature> query(Object compiledQuery, int limit, int offset);

    record GameFeature(
            UUID id,
            UUID requestId,
            String gameUrl,
            String platform,
            String whiteUsername,
            String blackUsername,
            Integer whiteElo,
            Integer blackElo,
            String timeClass,
            String eco,
            String result,
            Instant playedAt,
            Integer numMoves,
            boolean hasPin,
            boolean hasCrossPin,
            boolean hasFork,
            boolean hasSkewer,
            boolean hasDiscoveredAttack,
            String motifsJson,
            String pgn
    ) {}
}
