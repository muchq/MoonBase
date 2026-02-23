package com.muchq.games.one_d4.api;

import com.muchq.games.one_d4.api.dto.QueryRequest;
import jakarta.inject.Singleton;

@Singleton
public class QueryRequestValidator {
  private static final int MAX_QUERY_LENGTH = 4096;

  public void validate(QueryRequest request) {
    if (request.query() == null || request.query().isBlank()) {
      throw new IllegalArgumentException("query is required");
    }
    if (request.query().length() > MAX_QUERY_LENGTH) {
      throw new IllegalArgumentException(
          "query exceeds maximum length of " + MAX_QUERY_LENGTH + " characters");
    }
  }
}
