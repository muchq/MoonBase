package com.muchq.games.one_d4.api.dto;

import java.util.Map;

/**
 * One aggregation group: the group-by column values (keyed by canonical column name, e.g.
 * "opening_family") and the number of matching games.
 */
public record AggregateRow(Map<String, Object> group, long count) {}
