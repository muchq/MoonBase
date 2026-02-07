package com.muchq.games.one_d4.api.dto;

import java.time.Instant;

public record GameFeatureRow(
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
    boolean hasDiscoveredAttack) {
  public static GameFeatureRow fromStore(GameFeature row) {
    return new GameFeatureRow(
        row.gameUrl(),
        row.platform(),
        row.whiteUsername(),
        row.blackUsername(),
        row.whiteElo(),
        row.blackElo(),
        row.timeClass(),
        row.eco(),
        row.result(),
        row.playedAt(),
        row.numMoves(),
        row.hasPin(),
        row.hasCrossPin(),
        row.hasFork(),
        row.hasSkewer(),
        row.hasDiscoveredAttack());
  }
}
