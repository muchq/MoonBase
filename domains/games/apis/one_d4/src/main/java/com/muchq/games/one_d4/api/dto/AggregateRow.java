package com.muchq.games.one_d4.api.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import java.util.Map;

/**
 * One aggregation group: the group-by column values (keyed by canonical column name, e.g.
 * "opening_family") and the number of matching games.
 *
 * <p>The group map's inclusion is pinned to ALWAYS because NULL group keys are part of the wire
 * contract: an untitled opponent or a missing rating groups under an explicit {@code
 * "opponent_title": null}, which is how callers count what a {@code !=} filter excludes. The HTTP
 * mapper's default inclusion omits null map values — and drops {@code group} entirely once every
 * value is suppressed — turning the null bucket into a group of the wrong arity or no group at all.
 * AggregateResponseWireTest holds the raw bytes to this.
 */
public record AggregateRow(
    @JsonInclude(value = JsonInclude.Include.ALWAYS, content = JsonInclude.Include.ALWAYS)
        Map<String, Object> group,
    long count) {}
