package com.muchq.games.one_d4.api.dto;

import java.util.List;

/**
 * Request for POST /v1/aggregate: a ChessQL filter plus group-by fields, returning per-group
 * counts. {@code orderBy} currently supports only "count" (descending), which is also the default.
 * {@code player} is the optional perspective player for me.*, opponent.*, and outcome fields in the
 * filter; group-by fields are physical columns, plus — when {@code player} is set — the perspective
 * fields: categorical ones by value, the rating fields as fixed-width buckets like {@code
 * opponent.elo(200)}.
 */
public record AggregateRequest(
    String query, List<String> groupBy, String orderBy, int limit, String player) {
  public AggregateRequest {
    if (limit <= 0) limit = 50;
    if (limit > 1000) limit = 1000;
  }

  public AggregateRequest(String query, List<String> groupBy, String orderBy, int limit) {
    this(query, groupBy, orderBy, limit, null);
  }
}
