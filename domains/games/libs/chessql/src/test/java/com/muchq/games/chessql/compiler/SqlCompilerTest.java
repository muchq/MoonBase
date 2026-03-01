package com.muchq.games.chessql.compiler;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.chessql.ast.OrderByClause;
import com.muchq.games.chessql.ast.SequenceExpr;
import com.muchq.games.chessql.parser.ParsedQuery;
import com.muchq.games.chessql.parser.Parser;
import java.util.List;
import org.junit.Test;

public class SqlCompilerTest {

  private final SqlCompiler compiler = new SqlCompiler();

  private static final String BASE_PREFIX = "SELECT g.* FROM game_features g WHERE ";
  private static final String BASE_SUFFIX = " ORDER BY g.played_at DESC";

  private static String motifExists(String motif) {
    return "EXISTS (SELECT 1 FROM motif_occurrences mo"
        + " WHERE mo.game_url = g.game_url AND mo.motif = '"
        + motif
        + "')";
  }

  private static final String FORK_EXISTS =
      "EXISTS (SELECT 1 FROM motif_occurrences mo"
          + " WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK'"
          + " AND mo.is_discovered = FALSE AND mo.attacker IS NOT NULL"
          + " GROUP BY mo.ply, mo.attacker HAVING COUNT(*) >= 2)";

  private static final String DOUBLE_CHECK_EXISTS =
      "EXISTS (SELECT 1 FROM motif_occurrences mo"
          + " WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK'"
          + " AND (mo.target LIKE 'K%' OR mo.target LIKE 'k%')"
          + " GROUP BY mo.ply HAVING COUNT(*) >= 2)";

  private static final String DISCOVERED_CHECK_EXISTS =
      "EXISTS (SELECT 1 FROM motif_occurrences mo"
          + " WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK'"
          + " AND mo.is_discovered = TRUE"
          + " AND (mo.target LIKE 'K%' OR mo.target LIKE 'k%'))";

  private static final String CHECKMATE_EXISTS =
      "EXISTS (SELECT 1 FROM motif_occurrences mo"
          + " WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK'"
          + " AND mo.is_mate = TRUE)";

  @Test
  public void testSimpleComparison() {
    CompiledQuery result = compile("white.elo >= 2500");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + "white_elo >= ?" + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of(2500));
  }

  @Test
  public void testMotif() {
    CompiledQuery result = compile("motif(fork)");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + FORK_EXISTS + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testAndExpression() {
    CompiledQuery result = compile("white.elo >= 2500 AND motif(fork)");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "(white_elo >= ? AND " + FORK_EXISTS + ")" + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of(2500));
  }

  @Test
  public void testOrExpression() {
    CompiledQuery result = compile("motif(fork) OR motif(pin)");
    assertThat(result.selectSql())
        .isEqualTo(
            BASE_PREFIX + "(" + FORK_EXISTS + " OR " + motifExists("PIN") + ")" + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testNotExpression() {
    CompiledQuery result = compile("NOT motif(pin)");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "(NOT " + motifExists("PIN") + ")" + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testInExpression() {
    CompiledQuery result = compile("platform IN [\"lichess\", \"chess.com\"]");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "LOWER(platform) IN (LOWER(?), LOWER(?))" + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of("lichess", "chess.com"));
  }

  @Test
  public void testStringEqualityCaseInsensitive() {
    CompiledQuery result = compile("white.username = \"hikaru\"");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "LOWER(white_username) = LOWER(?)" + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of("hikaru"));
  }

  @Test
  public void testNumericEqualityNotWrapped() {
    CompiledQuery result = compile("white.elo = 3000");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + "white_elo = ?" + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of(3000));
  }

  @Test
  public void testComplexQuery() {
    CompiledQuery result = compile("white.elo >= 2500 AND motif(cross_pin)");
    assertThat(result.selectSql())
        .isEqualTo(
            BASE_PREFIX + "(white_elo >= ? AND " + motifExists("CROSS_PIN") + ")" + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of(2500));
  }

  @Test
  public void testNestedBooleans() {
    CompiledQuery result = compile("(motif(fork) OR motif(pin)) AND white.elo > 2000");
    assertThat(result.selectSql())
        .isEqualTo(
            BASE_PREFIX
                + "(("
                + FORK_EXISTS
                + " OR "
                + motifExists("PIN")
                + ") AND white_elo > ?)"
                + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of(2000));
  }

  @Test
  public void testCheckMotif() {
    CompiledQuery result = compile("motif(check)");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + motifExists("CHECK") + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testCheckmateMotif() {
    CompiledQuery result = compile("motif(checkmate)");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + CHECKMATE_EXISTS + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testPromotionMotif() {
    CompiledQuery result = compile("motif(promotion)");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + motifExists("PROMOTION") + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testPromotionWithCheckMotif() {
    CompiledQuery result = compile("motif(promotion_with_check)");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + motifExists("PROMOTION_WITH_CHECK") + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testPromotionWithCheckmateMotif() {
    CompiledQuery result = compile("motif(promotion_with_checkmate)");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + motifExists("PROMOTION_WITH_CHECKMATE") + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testDiscoveredCheckMotif() {
    CompiledQuery result = compile("motif(discovered_check)");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + DISCOVERED_CHECK_EXISTS + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testDiscoveredAttackMotif() {
    CompiledQuery result = compile("motif(discovered_attack)");
    assertThat(result.selectSql())
        .isEqualTo(
            BASE_PREFIX
                + "EXISTS (SELECT 1 FROM motif_occurrences mo"
                + " WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK'"
                + " AND mo.is_discovered = TRUE)"
                + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testBackRankMateMotif() {
    CompiledQuery result = compile("motif(back_rank_mate)");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + motifExists("BACK_RANK_MATE") + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testSmotheredMateMotif() {
    CompiledQuery result = compile("motif(smothered_mate)");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + motifExists("SMOTHERED_MATE") + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testSacrificeMotif() {
    CompiledQuery result = compile("motif(sacrifice)");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + motifExists("SACRIFICE") + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testZugzwangMotif() {
    CompiledQuery result = compile("motif(zugzwang)");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + motifExists("ZUGZWANG") + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testDoubleCheckMotif() {
    CompiledQuery result = compile("motif(double_check)");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + DOUBLE_CHECK_EXISTS + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
<<<<<<< Updated upstream
  public void testInterferenceMotif() {
    CompiledQuery result = compile("motif(interference)");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + motifExists("INTERFERENCE") + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
=======
>>>>>>> Stashed changes
  public void testOverloadedPieceMotif() {
    CompiledQuery result = compile("motif(overloaded_piece)");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + motifExists("OVERLOADED_PIECE") + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testUnknownMotif() {
    assertThatThrownBy(() -> compile("motif(unknown)"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unknown motif");
  }

  @Test
  public void testAttackMotifIsRejected() {
    assertThatThrownBy(() -> compile("motif(attack)"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unknown motif");
  }

  @Test
  public void testUnknownField() {
    assertThatThrownBy(() -> compile("bogus_field >= 100"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unknown field");
  }

  @Test
  public void testDirectColumnName() {
    CompiledQuery result = compile("white_elo >= 2500");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + "white_elo >= ?" + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of(2500));
  }

  @Test
  public void testOrderByMotifCount() {
    CompiledQuery result = compile("motif(promotion) ORDER BY motif_count(check) DESC");
    assertThat(result.selectSql())
        .isEqualTo(
            "SELECT g.* FROM game_features g"
                + " LEFT JOIN (SELECT game_url, COUNT(*) AS c FROM motif_occurrences"
                + " WHERE motif = ? GROUP BY game_url) cnt"
                + " ON g.game_url = cnt.game_url"
                + " WHERE "
                + motifExists("PROMOTION")
                + " ORDER BY COALESCE(cnt.c, 0) DESC");
    assertThat(result.parameters()).isEqualTo(List.of("CHECK"));
  }

  @Test
  public void testOrderByMotifCountAsc() {
    CompiledQuery result = compile("motif(fork) ORDER BY motif_count(pin) ASC");
    assertThat(result.selectSql())
        .isEqualTo(
            "SELECT g.* FROM game_features g"
                + " LEFT JOIN (SELECT game_url, COUNT(*) AS c FROM motif_occurrences"
                + " WHERE motif = ? GROUP BY game_url) cnt"
                + " ON g.game_url = cnt.game_url"
                + " WHERE "
                + FORK_EXISTS
                + " ORDER BY COALESCE(cnt.c, 0) ASC");
    assertThat(result.parameters()).isEqualTo(List.of("PIN"));
  }

  @Test
  public void testOrderByMotifCountWithWhereParams() {
    CompiledQuery result =
        compile("white.elo >= 2500 AND motif(fork) ORDER BY motif_count(check) DESC");
    // The LEFT JOIN param (CHECK) must come before WHERE params
    assertThat(result.parameters()).isEqualTo(List.of("CHECK", 2500));
  }

  @Test
  public void testOrderByForkUsesAttackDerivedCountSubquery() {
    CompiledQuery result = compile("motif(pin) ORDER BY motif_count(fork) DESC");
    String forkCountSq =
        "SELECT game_url, COUNT(*) AS c FROM ("
            + "SELECT game_url FROM motif_occurrences"
            + " WHERE motif = 'ATTACK' AND is_discovered = FALSE AND attacker IS NOT NULL"
            + " GROUP BY game_url, ply, attacker HAVING COUNT(*) >= 2"
            + ") forks GROUP BY game_url";
    assertThat(result.selectSql())
        .isEqualTo(
            "SELECT g.* FROM game_features g"
                + " LEFT JOIN ("
                + forkCountSq
                + ") cnt"
                + " ON g.game_url = cnt.game_url"
                + " WHERE "
                + motifExists("PIN")
                + " ORDER BY COALESCE(cnt.c, 0) DESC");
    // No extra param for fork — ATTACK is inlined as a literal
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testOrderByForkWithWhereParams() {
    CompiledQuery result =
        compile("white.elo >= 2500 AND motif(fork) ORDER BY motif_count(fork) DESC");
    // For derived ORDER BY, WHERE params come first (no JOIN param prepended)
    assertThat(result.parameters()).isEqualTo(List.of(2500));
  }

  @Test
  public void testOrderByUnknownMotif() {
    assertThatThrownBy(() -> compile("motif(fork) ORDER BY motif_count(unknown) DESC"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unknown motif in ORDER BY");
  }

  private static final String FORK_PLY_SQ =
      "SELECT game_url, ply FROM motif_occurrences"
          + " WHERE motif = 'ATTACK' AND is_discovered = FALSE AND attacker IS NOT NULL"
          + " GROUP BY game_url, ply, attacker HAVING COUNT(*) >= 2";

  private static final String CHECKMATE_PLY_SQ =
      "SELECT game_url, ply FROM motif_occurrences WHERE motif = 'ATTACK' AND is_mate = TRUE";

  private static final String DISCOVERED_CHECK_PLY_SQ =
      "SELECT game_url, ply FROM motif_occurrences"
          + " WHERE motif = 'ATTACK' AND is_discovered = TRUE"
          + " AND (target LIKE 'K%' OR target LIKE 'k%')";

  private static String storedPlySubquery(String upperMotif) {
    return "SELECT game_url, ply FROM motif_occurrences WHERE motif = '" + upperMotif + "'";
  }

  private static String sequenceExists(String... sqFragments) {
    StringBuilder sb = new StringBuilder("EXISTS (SELECT 1 FROM (");
    sb.append(sqFragments[0]).append(") sq1");
    for (int i = 1; i < sqFragments.length; i++) {
      int sqNum = i + 1;
      sb.append(" JOIN (")
          .append(sqFragments[i])
          .append(") sq")
          .append(sqNum)
          .append(" ON sq")
          .append(sqNum)
          .append(".game_url = sq1.game_url AND sq")
          .append(sqNum)
          .append(".ply = sq")
          .append(i)
          .append(".ply + 2");
    }
    sb.append(" WHERE sq1.game_url = g.game_url)");
    return sb.toString();
  }

  @Test
  public void testSequenceTwoStoredMotifs() {
    CompiledQuery result = compile("sequence(pin THEN skewer)");
    assertThat(result.selectSql())
        .isEqualTo(
            BASE_PREFIX
                + sequenceExists(storedPlySubquery("PIN"), storedPlySubquery("SKEWER"))
                + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testSequenceTwoMotifs() {
    CompiledQuery result = compile("sequence(discovered_check THEN checkmate)");
    assertThat(result.selectSql())
        .isEqualTo(
            BASE_PREFIX + sequenceExists(DISCOVERED_CHECK_PLY_SQ, CHECKMATE_PLY_SQ) + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testSequenceThreeMotifs() {
    CompiledQuery result = compile("sequence(fork THEN check THEN checkmate)");
    assertThat(result.selectSql())
        .isEqualTo(
            BASE_PREFIX
                + sequenceExists(FORK_PLY_SQ, storedPlySubquery("CHECK"), CHECKMATE_PLY_SQ)
                + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testSequenceTooShort() {
    assertThatThrownBy(
            () -> compiler.compile(new ParsedQuery(new SequenceExpr(List.of("fork")), null)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("sequence() requires at least 2 motifs");
  }

  @Test
  public void testSequenceUnknownMotif() {
    assertThatThrownBy(() -> compile("sequence(fork THEN unknown)"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unknown motif in sequence");
  }

  @Test
  public void testOrderByClausePreserved() {
    ParsedQuery pq = Parser.parse("motif(check) ORDER BY motif_count(checkmate) DESC");
    assertThat(pq.orderBy()).isNotNull();
    OrderByClause orderBy = pq.orderBy();
    assertThat(orderBy.motifName()).isEqualTo("checkmate");
    assertThat(orderBy.ascending()).isFalse();
  }

  @Test
  public void testNoOrderBy() {
    ParsedQuery pq = Parser.parse("motif(fork)");
    assertThat(pq.orderBy()).isNull();
  }

  private CompiledQuery compile(String input) {
    return compiler.compile(Parser.parse(input));
  }
}
