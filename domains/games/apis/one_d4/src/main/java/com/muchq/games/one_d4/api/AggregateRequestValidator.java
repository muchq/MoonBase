package com.muchq.games.one_d4.api;

import com.muchq.games.chessql.compiler.AggregateSpec;
import com.muchq.games.one_d4.api.dto.AggregateRequest;
import jakarta.inject.Singleton;

/**
 * Validates a POST /v1/aggregate request and hands back the {@link AggregateSpec} the compiler
 * takes.
 *
 * <p>Returning the spec rather than {@code void} is the point: the spec's own constructor enforces
 * the rules that are about the shape of an aggregate (a score ranking needs a player), so building
 * it here means the endpoint checks them exactly once, in the same place it checks everything else,
 * and cannot drift from what the compiler will accept a moment later.
 */
@Singleton
public class AggregateRequestValidator {
  private static final int MAX_QUERY_LENGTH = 4096;
  private static final int MAX_GROUP_BY_FIELDS = 5;

  public AggregateSpec validate(AggregateRequest request) {
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
    return new AggregateSpec(
        request.groupBy(),
        request.player(),
        AggregateSpec.Order.fromWire(request.orderBy()),
        request.minGames());
  }
}
