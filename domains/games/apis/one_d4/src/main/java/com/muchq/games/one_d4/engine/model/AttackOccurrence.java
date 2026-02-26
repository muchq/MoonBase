package com.muchq.games.one_d4.engine.model;

/**
 * Records a single significant attack at a given ply.
 *
 * <p>Fields use piece+square notation (e.g. "Ne6", "ke8") produced by {@code
 * DiscoveredAttackDetector}.
 *
 * <ul>
 *   <li>{@code pieceMoved} — what physically moved this ply (e.g. "Nh4")
 *   <li>{@code attacker} — piece making the attack (e.g. "Ng6"); equals {@code pieceMoved} for
 *       direct attacks, differs for discovered attacks
 *   <li>{@code attacked} — piece being attacked (e.g. "ke8", "Qd8")
 *   <li>{@code isCheckmate} — true when the move ended in {@code #}
 * </ul>
 *
 * <p>Derived motif queries:
 *
 * <ul>
 *   <li>Fork: {@code GROUP BY game_url, ply, attacker HAVING count(*) >= 2}
 *   <li>Check: {@code WHERE attacked LIKE 'k%' OR attacked LIKE 'K%'}
 *   <li>Discovered check: check row {@code WHERE piece_moved != attacker}
 * </ul>
 */
public record AttackOccurrence(
    int ply,
    int moveNumber,
    String side,
    String pieceMoved,
    String attacker,
    String attacked,
    boolean isCheckmate) {}
