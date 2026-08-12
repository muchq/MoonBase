package com.muchq.games.one_d4.api.dto;

import java.util.List;

/**
 * Request for POST /v1/aggregate: a ChessQL filter plus group-by fields, returning per-group counts
 * — and, when {@code player} is set, per-group win/loss/draw/score. {@code orderBy} is "count"
 * (descending, the default) or "score" — the player's (W + D/2) per game, highest first, so it
 * needs a player. {@code
 * minGames} drops groups with fewer games than that, which is what makes a score ranking readable:
 * without it the top of the list is the sideline played once and won. {@code player} is the
 * optional perspective player for me.*, opponent.*, and outcome fields in the filter; group-by
 * fields are physical columns, plus — when {@code player} is set — the perspective fields:
 * categorical ones by value, the rating fields as fixed-width buckets like {@code
 * opponent.elo(200)}.
 */
public record AggregateRequest(
    String query, List<String> groupBy, String orderBy, int limit, String player, int minGames) {
  public AggregateRequest {
    if (limit <= 0) limit = 50;
    if (limit > 1000) limit = 1000;
    // A negative floor is not a different request from no floor at all; clamped like limit rather
    // than rejected, since every group has at least one game.
    if (minGames < 0) minGames = 0;
  }

  public AggregateRequest(String query, List<String> groupBy, String orderBy, int limit) {
    this(query, groupBy, orderBy, limit, null, 0);
  }

  public AggregateRequest(
      String query, List<String> groupBy, String orderBy, int limit, String player) {
    this(query, groupBy, orderBy, limit, player, 0);
  }
}
