package com.muchq.games.one_d4.api.dto;

import java.time.Instant;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public record GameFeatureRow(
    String gameUrl,
    String platform,
    String whiteUsername,
    String blackUsername,
    Integer whiteElo,
    Integer blackElo,
    String whiteTitle,
    String blackTitle,
    String timeClass,
    String eco,
    String openingName,
    String openingFamily,
    String result,
    Instant playedAt,
    Instant indexedAt,
    Integer numMoves,
    String pgn,
    Map<String, List<OccurrenceRow>> occurrences) {

  public static GameFeatureRow fromStore(
      GameFeature row, Map<String, List<OccurrenceRow>> occurrences) {
    // Copy rather than alias the DAO's mutable map and lists: rows built here can outlive the
    // request inside FirstPageCache's snapshot, read concurrently by every request thread.
    // LinkedHashMap keeps the store's motif ordering, unlike Map.copyOf.
    Map<String, List<OccurrenceRow>> copied = new LinkedHashMap<>();
    occurrences.forEach((motif, rows) -> copied.put(motif, List.copyOf(rows)));
    return new GameFeatureRow(
        row.gameUrl(),
        row.platform(),
        row.whiteUsername(),
        row.blackUsername(),
        row.whiteElo(),
        row.blackElo(),
        row.whiteTitle(),
        row.blackTitle(),
        row.timeClass(),
        row.eco(),
        row.openingName(),
        row.openingFamily(),
        row.result(),
        row.playedAt(),
        row.indexedAt(),
        row.numMoves(),
        row.pgn(),
        copied);
  }
}
