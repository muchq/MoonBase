package com.muchq.games.one_d4.api.dto;

/**
 * @param player optional chess.com username that perspective fields (me.*, opponent.*, outcome) in
 *     the query are resolved against; required when the query uses them
 */
public record QueryRequest(String query, int limit, int offset, String player) {
  public QueryRequest {
    if (limit <= 0) limit = 50;
    if (limit > 1000) limit = 1000;
    if (offset < 0) offset = 0;
  }

  public QueryRequest(String query, int limit, int offset) {
    this(query, limit, offset, null);
  }
}
