package com.muchq.games.chessql.compiler;

import java.util.Arrays;
import java.util.List;
import java.util.stream.Collectors;

/**
 * Everything an aggregate needs beyond its ChessQL filter: what to group by, whose perspective the
 * {@code me.*} / {@code opponent.*} / {@code outcome} fields resolve against, how the groups are
 * ranked, and the smallest group worth reporting.
 *
 * <p>One record rather than four positional parameters because the last two only make sense
 * together with the first two: {@code score} is a player's W + D/2, so ordering by it without a
 * player is not a query anyone can answer, and this constructor refuses it rather than compiling
 * SQL that references a column the SELECT list would not carry.
 *
 * <p>{@link #hasOutcomeMetrics()} is the single answer to "does this aggregate's result carry
 * wins/losses/draws?" — the compiler decides what to SELECT from it and the caller decides what to
 * read back from it, so the two cannot disagree about the shape of a row.
 */
public record AggregateSpec(List<String> groupBy, String player, Order order, int minGames) {

  /**
   * How groups are ranked before the limit cuts the tail off. Ranking is a server-side concern
   * precisely because it happens <em>before</em> truncation: a client sorting the rows it received
   * can only reorder what survived the limit.
   */
  public enum Order {
    /** Most games first. The default, and the only ordering available without a player. */
    COUNT("count"),
    /**
     * Best score <em>per game</em> first — (W + D/2) / games — with ties broken by game count.
     * Requires a player. Ranking by total points instead would only re-spell {@link #COUNT}: the
     * most-played group collects the most points almost by definition.
     */
    SCORE("score");

    private final String wireName;

    Order(String wireName) {
      this.wireName = wireName;
    }

    /** The name this ordering is requested by over the wire. */
    public String wireName() {
      return wireName;
    }

    /** Every accepted spelling, for error messages and docs. Derived, so it cannot drift. */
    public static String roster() {
      return Arrays.stream(values())
          .map(o -> "\"" + o.wireName + "\"")
          .collect(Collectors.joining(", "));
    }

    /**
     * Resolves a requested ordering, defaulting to {@link #COUNT} when absent — an omitted {@code
     * orderBy} has always meant "most games first" and continues to.
     */
    public static Order fromWire(String value) {
      if (value == null || value.isBlank()) {
        return COUNT;
      }
      for (Order order : values()) {
        if (order.wireName.equalsIgnoreCase(value.strip())) {
          return order;
        }
      }
      throw new IllegalArgumentException(
          "orderBy supports " + roster() + ", not \"" + value + "\"");
    }
  }

  public AggregateSpec {
    groupBy = groupBy == null ? List.of() : List.copyOf(groupBy);
    // Normalized here as well as in the compiler's Perspective, so hasOutcomeMetrics() and the
    // compiled SELECT list agree on what "has a player" means for a blank string.
    player = player == null || player.isBlank() ? null : player.strip();
    order = order == null ? Order.COUNT : order;
    minGames = Math.max(minGames, 0);
    if (order == Order.SCORE && player == null) {
      throw new IllegalArgumentException(
          "orderBy \"score\" requires a player: score is that player's W + D/2 per game, and with"
              + " no perspective there is no side to score for. Supply player, or order by"
              + " \"count\".");
    }
  }

  /** Grouping alone, over the whole corpus: no perspective player, no ranking beyond count. */
  public static AggregateSpec of(List<String> groupBy) {
    return new AggregateSpec(groupBy, null, Order.COUNT, 0);
  }

  /** Grouping from a player's perspective, ranked by count. */
  public static AggregateSpec of(List<String> groupBy, String player) {
    return new AggregateSpec(groupBy, player, Order.COUNT, 0);
  }

  /**
   * Whether the compiled aggregate carries per-group {@code wins} / {@code losses} / {@code draws}
   * / {@code score_points} alongside {@code group_count}.
   *
   * <p>Tied to the player rather than to a flag of its own: the metrics are outcomes <em>from
   * someone's point of view</em>, so a corpus-wide aggregate has nothing to report and a
   * player-scoped one always does. Every player-scoped aggregate already pays for the perspective
   * machinery; four more SUMs over the same scan is not a second decision worth exposing.
   */
  public boolean hasOutcomeMetrics() {
    return player != null;
  }
}
