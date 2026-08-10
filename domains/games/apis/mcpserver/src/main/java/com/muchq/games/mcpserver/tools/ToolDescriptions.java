package com.muchq.games.mcpserver.tools;

/**
 * Caveats that belong in more than one tool description.
 *
 * <p>A description is the whole of what a model knows before it decides to call, so a caveat that
 * applies to two tools has to be written into both — and two copies of a paragraph drift. These are
 * compile-time constants, so each still lands in its {@code @Tool} annotation as a single literal.
 *
 * <p>Worded to be true at every site that uses them: {@code query_chess_games} returns games and
 * {@code aggregate_chess_games} returns groups, so the shared text names neither.
 */
final class ToolDescriptions {

  /**
   * Both query tools see only what has been indexed, and neither reports which periods those are —
   * so a date-scoped miss is indistinguishable from the player not having played.
   */
  static final String UNINDEXED_PERIODS_READ_AS_EMPTY =
      "date and month filter the indexed corpus only, so a period that was never indexed comes back"
          + " empty rather than as an error — indistinguishable from 'played no games then' unless"
          + " you index that period first with index_chess_games.";

  /** Spelled both ways: ChessQL takes the dotted form, group_by the underscored one. */
  static final String OPENING_FAMILY_IS_NOT_NORMALIZED =
      "Note: opening family (opening.family in a query, opening_family in group_by) is derived from"
          + " chess.com ECO-URL strings, not a normalized taxonomy — 'Closed Sicilian Defense' and"
          + " 'Alapin Sicilian Defense' are distinct values, not part of 'Sicilian Defense'.";

  private ToolDescriptions() {}
}
