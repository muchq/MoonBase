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

  @Test
  public void testSimpleComparison() {
    CompiledQuery result = compile("white.elo >= 2500");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + "white_elo >= ?" + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of(2500));
  }

  @Test
  public void testMotif() {
    CompiledQuery result = compile("motif(fork)");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + "g.has_fork = TRUE" + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testAndExpression() {
    CompiledQuery result = compile("white.elo >= 2500 AND motif(fork)");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "(white_elo >= ? AND g.has_fork = TRUE)" + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of(2500));
  }

  @Test
  public void testOrExpression() {
    CompiledQuery result = compile("motif(fork) OR motif(pin)");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "(g.has_fork = TRUE OR g.has_pin = TRUE)" + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testNotExpression() {
    CompiledQuery result = compile("NOT motif(pin)");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + "(NOT g.has_pin = TRUE)" + BASE_SUFFIX);
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
        .isEqualTo(BASE_PREFIX + "(white_elo >= ? AND g.has_cross_pin = TRUE)" + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of(2500));
  }

  @Test
  public void testNestedBooleans() {
    CompiledQuery result = compile("(motif(fork) OR motif(pin)) AND white.elo > 2000");
    assertThat(result.selectSql())
        .isEqualTo(
            BASE_PREFIX
                + "((g.has_fork = TRUE OR g.has_pin = TRUE) AND white_elo > ?)"
                + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of(2000));
  }

  @Test
  public void testCheckMotif() {
    CompiledQuery result = compile("motif(check)");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + "g.has_check = TRUE" + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testCheckmateMotif() {
    CompiledQuery result = compile("motif(checkmate)");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + "g.has_checkmate = TRUE" + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testPromotionMotif() {
    CompiledQuery result = compile("motif(promotion)");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + "g.has_promotion = TRUE" + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testPromotionWithCheckMotif() {
    CompiledQuery result = compile("motif(promotion_with_check)");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "g.has_promotion_with_check = TRUE" + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testPromotionWithCheckmateMotif() {
    CompiledQuery result = compile("motif(promotion_with_checkmate)");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "g.has_promotion_with_checkmate = TRUE" + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testDiscoveredCheckMotif() {
    CompiledQuery result = compile("motif(discovered_check)");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "g.has_discovered_check = TRUE" + BASE_SUFFIX);
    assertThat(result.parameters()).isEmpty();
  }

  @Test
  public void testUnknownMotif() {
    assertThatThrownBy(() -> compile("motif(unknown)"))
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
                + " WHERE g.has_promotion = TRUE"
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
                + " WHERE g.has_fork = TRUE"
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
  public void testOrderByUnknownMotif() {
    assertThatThrownBy(() -> compile("motif(fork) ORDER BY motif_count(unknown) DESC"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unknown motif in ORDER BY");
  }

  @Test
  public void testSequenceTwoMotifs() {
    CompiledQuery result = compile("sequence(discovered_check THEN checkmate)");
    assertThat(result.selectSql())
        .isEqualTo(
            BASE_PREFIX
                + "EXISTS (SELECT 1 FROM motif_occurrences mo1"
                + " JOIN motif_occurrences mo2 ON mo2.game_url = mo1.game_url"
                + " AND mo2.motif = ? AND mo2.ply = mo1.ply + 2"
                + " WHERE mo1.game_url = g.game_url AND mo1.motif = ?)"
                + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of("CHECKMATE", "DISCOVERED_CHECK"));
  }

  @Test
  public void testSequenceThreeMotifs() {
    CompiledQuery result = compile("sequence(fork THEN check THEN checkmate)");
    assertThat(result.selectSql())
        .isEqualTo(
            BASE_PREFIX
                + "EXISTS (SELECT 1 FROM motif_occurrences mo1"
                + " JOIN motif_occurrences mo2 ON mo2.game_url = mo1.game_url"
                + " AND mo2.motif = ? AND mo2.ply = mo1.ply + 2"
                + " JOIN motif_occurrences mo3 ON mo3.game_url = mo1.game_url"
                + " AND mo3.motif = ? AND mo3.ply = mo2.ply + 2"
                + " WHERE mo1.game_url = g.game_url AND mo1.motif = ?)"
                + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of("CHECK", "CHECKMATE", "FORK"));
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
