package com.muchq.games.one_d4.api;

import com.muchq.games.one_d4.api.dto.AggregateRequest;
import jakarta.inject.Singleton;

@Singleton
public class AggregateRequestValidator {
  private static final int MAX_QUERY_LENGTH = 4096;
  private static final int MAX_GROUP_BY_FIELDS = 5;

  public void validate(AggregateRequest request) {
    if (request.query() == null || request.query().isBlank()) {
      throw new IllegalArgumentException("query is required");
    }
    if (request.query().length() > MAX_QUERY_LENGTH) {
      throw new IllegalArgumentException(
          "query exceeds maximum length of " + MAX_QUERY_LENGTH + " characters");
    }
    if (request.groupBy() == null || request.groupBy().isEmpty()) {
      throw new IllegalArgumentException("groupBy requires at least one field");
    }
    if (request.groupBy().size() > MAX_GROUP_BY_FIELDS) {
      throw new IllegalArgumentException(
          "groupBy supports at most " + MAX_GROUP_BY_FIELDS + " fields");
    }
    for (String field : request.groupBy()) {
      if (field == null || field.isBlank()) {
        throw new IllegalArgumentException("groupBy fields must not be blank");
      }
    }
    if (request.orderBy() != null && !"count".equalsIgnoreCase(request.orderBy())) {
      throw new IllegalArgumentException("orderBy supports only \"count\"");
    }
  }
}
