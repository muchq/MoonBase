package com.muchq.games.one_d4.api.dto;

import org.jspecify.annotations.Nullable;

public record QueryRequest(
    String query,
    int limit,
    int offset,
    @Nullable Boolean includePgn,
    @Nullable Boolean includeOccurrences) {
  public QueryRequest {
    if (limit <= 0) limit = 50;
    if (limit > 1000) limit = 1000;
    if (offset < 0) offset = 0;
  }

  /** True if PGN should be included; defaults to true when null (backward compatibility). */
  public boolean includePgnOrDefault() {
    return includePgn == null || includePgn;
  }

  /**
   * True if occurrences should be included; defaults to true when null (backward compatibility).
   */
  public boolean includeOccurrencesOrDefault() {
    return includeOccurrences == null || includeOccurrences;
  }
}
