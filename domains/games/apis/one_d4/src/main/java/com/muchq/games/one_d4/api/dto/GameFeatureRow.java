package com.muchq.games.one_d4.api.dto;

import java.time.Instant;
import java.util.List;
import java.util.Map;

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
    Instant indexedAt,
    Integer numMoves,
    String pgn,
    Map<String, List<OccurrenceRow>> occurrences) {

  public static GameFeatureRow fromStore(
      GameFeature row, Map<String, List<OccurrenceRow>> occurrences) {
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
        row.indexedAt(),
        row.numMoves(),
        row.pgn(),
        occurrences);
  }
}
