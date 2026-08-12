package com.muchq.games.one_d4.api.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import java.util.Map;

/**
 * One aggregation group: the group-by column values (keyed by canonical column name, e.g.
 * "opening_family"), the number of matching games, and — for a player-scoped aggregate — how that
 * player did in them.
 *
 * <p>The group map's inclusion is pinned to ALWAYS because NULL group keys are part of the wire
 * contract: an untitled opponent or a missing rating groups under an explicit {@code
 * "opponent_title": null}, which is how callers count what a {@code !=} filter excludes. The HTTP
 * mapper's default inclusion omits null map values — and drops {@code group} entirely once every
 * value is suppressed — turning the null bucket into a group of the wrong arity or no group at all.
 * AggregateResponseWireTest holds the raw bytes to this.
 *
 * <p>The outcome metrics are the opposite case, and are pinned to NON_NULL for it. Without a player
 * there is no side to attribute a result to, so the fields are <em>absent</em> rather than zero: a
 * zero would read as "played none of these openings as a win", which is a claim this row cannot
 * make. Present-and-zero is a real answer (a family the player has never won); absent is "not
 * asked". Only an explicit inclusion makes both mappers agree — the container's omits nulls, {@code
 * JsonUtils}' (the MCP tool path) writes them.
 */
public record AggregateRow(
    @JsonInclude(value = JsonInclude.Include.ALWAYS, content = JsonInclude.Include.ALWAYS)
        Map<String, Object> group,
    long count,
    @JsonInclude(JsonInclude.Include.NON_NULL) Long wins,
    @JsonInclude(JsonInclude.Include.NON_NULL) Long losses,
    @JsonInclude(JsonInclude.Include.NON_NULL) Long draws,
    @JsonInclude(JsonInclude.Include.NON_NULL) Double score) {

  /** A group with no perspective player: a count, and no outcome to attribute. */
  public AggregateRow(Map<String, Object> group, long count) {
    this(group, count, null, null, null, null);
  }

  /**
   * A group counted from a player's perspective, from the half-points the SQL sums ({@code
   * score_points}: 2 per win, 1 per draw). Halving here rather than in SQL keeps the database
   * summing integers while the wire still carries the conventional W + D/2 — 20.5 out of 40, not
   * 41.
   *
   * <p>{@code wins + losses + draws} can be less than {@code count}: a game whose result is neither
   * a decision nor a draw (an unfinished {@code *}) is counted and scored nowhere.
   */
  public static AggregateRow withOutcomes(
      Map<String, Object> group, long count, long wins, long losses, long draws, long scorePoints) {
    return new AggregateRow(group, count, wins, losses, draws, scorePoints / 2.0);
  }
}
