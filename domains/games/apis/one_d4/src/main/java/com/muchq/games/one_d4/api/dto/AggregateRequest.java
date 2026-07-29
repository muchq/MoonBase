package com.muchq.games.one_d4.api.dto;

import java.util.List;

/**
 * Request for POST /v1/aggregate: a ChessQL filter plus group-by fields, returning per-group
 * counts. {@code orderBy} currently supports only "count" (descending), which is also the default.
 */
public record AggregateRequest(String query, List<String> groupBy, String orderBy, int limit) {
  public AggregateRequest {
    if (limit <= 0) limit = 50;
    if (limit > 1000) limit = 1000;
  }
}
