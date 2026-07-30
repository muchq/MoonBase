package com.muchq.games.one_d4.api.dto;

import java.util.List;

/**
 * Response for POST /v1/aggregate. {@code groups} is capped by the request limit and {@code count}
 * is the number of groups returned; {@code totalGames} and {@code totalGroups} are computed over
 * the untruncated result so callers can tell when a long tail was cut off ({@code truncated} is
 * {@code totalGroups > count}).
 */
public record AggregateResponse(
    List<AggregateRow> groups, int count, long totalGames, long totalGroups, boolean truncated) {}
