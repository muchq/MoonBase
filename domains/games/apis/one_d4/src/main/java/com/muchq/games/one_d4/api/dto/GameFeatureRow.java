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
    boolean hasPin,
    boolean hasCrossPin,
    boolean hasFork,
    boolean hasSkewer,
    boolean hasDiscoveredAttack,
    boolean hasDiscoveredCheck,
    boolean hasCheck,
    boolean hasCheckmate,
    boolean hasPromotion,
    boolean hasPromotionWithCheck,
    boolean hasPromotionWithCheckmate,
    String pgn,
    Map<String, List<OccurrenceRow>> occurrences,
    List<AttackOccurrenceRow> attacks) {

  public static GameFeatureRow fromStore(
      GameFeature row,
      Map<String, List<OccurrenceRow>> occurrences,
      List<AttackOccurrenceRow> attacks) {
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
        row.hasPin(),
        row.hasCrossPin(),
        row.hasFork(),
        row.hasSkewer(),
        row.hasDiscoveredAttack(),
        row.hasDiscoveredCheck(),
        row.hasCheck(),
        row.hasCheckmate(),
        row.hasPromotion(),
        row.hasPromotionWithCheck(),
        row.hasPromotionWithCheckmate(),
        row.pgn(),
        occurrences,
        attacks);
  }
}
