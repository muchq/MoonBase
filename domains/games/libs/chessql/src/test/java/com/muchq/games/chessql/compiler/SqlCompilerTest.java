package com.muchq.games.chessql.compiler;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.chessql.ast.OrderByClause;
import com.muchq.games.chessql.ast.SequenceExpr;
import com.muchq.games.chessql.parser.ParsedQuery;
import com.muchq.games.chessql.parser.Parser;
import java.sql.Timestamp;
import java.time.Instant;
import java.util.List;
import org.junit.jupiter.api.Test;

public class SqlCompilerTest {

  private final SqlCompiler compiler = new SqlCompiler();

  private static final String BASE_PREFIX = "SELECT g.* FROM game_features g WHERE ";
  private static final String BASE_SUFFIX = " ORDER BY g.played_at DESC, g.game_url ASC";

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
                + " ORDER BY COALESCE(cnt.c, 0) DESC, g.game_url ASC");
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
                + " ORDER BY COALESCE(cnt.c, 0) ASC, g.game_url ASC");
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
                + " ORDER BY COALESCE(cnt.c, 0) DESC, g.game_url ASC");
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

  @Test
  public void testTitleFieldsCompileToCaseInsensitiveComparison() {
    CompiledQuery white = compile("white.title = \"GM\"");
    assertThat(white.selectSql())
        .isEqualTo(BASE_PREFIX + "LOWER(white_title) = LOWER(?)" + BASE_SUFFIX);
    assertThat(white.parameters()).isEqualTo(List.of("GM"));

    CompiledQuery black = compile("black.title != \"GM\"");
    assertThat(black.selectSql())
        .isEqualTo(BASE_PREFIX + "LOWER(black_title) != LOWER(?)" + BASE_SUFFIX);
  }

  @Test
  public void testTitleFieldSupportsIn() {
    CompiledQuery result = compile("black.title IN [\"GM\", \"IM\"]");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "LOWER(black_title) IN (LOWER(?), LOWER(?))" + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of("GM", "IM"));
  }

  @Test
  public void testOpeningFieldsCompileToCaseInsensitiveComparison() {
    CompiledQuery family = compile("opening.family = \"Caro Kann Defense\"");
    assertThat(family.selectSql())
        .isEqualTo(BASE_PREFIX + "LOWER(opening_family) = LOWER(?)" + BASE_SUFFIX);
    assertThat(family.parameters()).isEqualTo(List.of("Caro Kann Defense"));

    CompiledQuery name = compile("opening.name = \"English Opening Agincourt Defense\"");
    assertThat(name.selectSql())
        .isEqualTo(BASE_PREFIX + "LOWER(opening_name) = LOWER(?)" + BASE_SUFFIX);
  }

  @Test
  public void testOpeningFieldsUnderscoreFormAccepted() {
    CompiledQuery result = compile("opening_family = \"Sicilian Defense\"");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "LOWER(opening_family) = LOWER(?)" + BASE_SUFFIX);
  }

  // === date / month scoping ===

  private static Timestamp utc(String instant) {
    return Timestamp.from(Instant.parse(instant));
  }

  @Test
  public void testDateGreaterOrEqualBindsStartOfDay() {
    CompiledQuery result = compile("date >= \"2026-07-01\"");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + "played_at >= ?" + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of(utc("2026-07-01T00:00:00Z")));
  }

  @Test
  public void testDateLessThanBindsStartOfDay() {
    CompiledQuery result = compile("date < \"2026-07-01\"");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + "played_at < ?" + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of(utc("2026-07-01T00:00:00Z")));
  }

  @Test
  public void testDateInclusiveUpperBoundCoversWholeDay() {
    CompiledQuery lte = compile("date <= \"2026-07-01\"");
    assertThat(lte.selectSql()).isEqualTo(BASE_PREFIX + "played_at < ?" + BASE_SUFFIX);
    assertThat(lte.parameters()).isEqualTo(List.of(utc("2026-07-02T00:00:00Z")));

    CompiledQuery gt = compile("date > \"2026-07-01\"");
    assertThat(gt.selectSql()).isEqualTo(BASE_PREFIX + "played_at >= ?" + BASE_SUFFIX);
    assertThat(gt.parameters()).isEqualTo(List.of(utc("2026-07-02T00:00:00Z")));
  }

  @Test
  public void testDateEqualityCompilesToDayRange() {
    CompiledQuery result = compile("date = \"2026-07-01\"");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "(played_at >= ? AND played_at < ?)" + BASE_SUFFIX);
    assertThat(result.parameters())
        .isEqualTo(List.of(utc("2026-07-01T00:00:00Z"), utc("2026-07-02T00:00:00Z")));
  }

  @Test
  public void testDateInequalityExcludesDayRange() {
    CompiledQuery result = compile("date != \"2026-07-01\"");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "(played_at < ? OR played_at >= ?)" + BASE_SUFFIX);
    assertThat(result.parameters())
        .isEqualTo(List.of(utc("2026-07-01T00:00:00Z"), utc("2026-07-02T00:00:00Z")));
  }

  @Test
  public void testMonthEqualityCompilesToMonthRange() {
    CompiledQuery result = compile("month = \"2026-07\"");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "(played_at >= ? AND played_at < ?)" + BASE_SUFFIX);
    assertThat(result.parameters())
        .isEqualTo(List.of(utc("2026-07-01T00:00:00Z"), utc("2026-08-01T00:00:00Z")));
  }

  @Test
  public void testMonthRejectsNonEqualityOperators() {
    assertThatThrownBy(() -> compile("month >= \"2026-07\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("month supports only '='");
  }

  @Test
  public void testDateRejectsMalformedValues() {
    assertThatThrownBy(() -> compile("date >= \"July 1, 2026\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("ISO date string");
    assertThatThrownBy(() -> compile("date >= 20260701"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("ISO date string");
    // A bare month is not a date; the error should steer to the month field's format
    assertThatThrownBy(() -> compile("date >= \"2026-07\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("ISO date string");
  }

  @Test
  public void testMonthRejectsMalformedValues() {
    assertThatThrownBy(() -> compile("month = \"2026-7\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("YYYY-MM");
    assertThatThrownBy(() -> compile("month = \"2026-07-01\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("YYYY-MM");
    assertThatThrownBy(() -> compile("month = 202607"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("YYYY-MM");
  }

  @Test
  public void testDateAndMonthRejectIn() {
    assertThatThrownBy(() -> compile("date IN [\"2026-07-01\", \"2026-07-02\"]"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("does not support IN");
    assertThatThrownBy(() -> compile("month IN [\"2026-06\", \"2026-07\"]"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("does not support IN");
  }

  @Test
  public void testDateCombinedWithPerspectiveFieldKeepsParamOrder() {
    CompiledQuery result =
        compiler.compile(Parser.parse("outcome = \"win\" AND date >= \"2026-07-01\""), "hikaru");

    assertThat(result.selectSql())
        .isEqualTo(
            BASE_PREFIX
                + "("
                + PARTICIPATION_GUARD
                + " AND (LOWER(CASE WHEN result = '1/2-1/2' THEN 'draw'"
                + " WHEN (result = '1-0' AND LOWER(white_username) = LOWER(?))"
                + " OR (result = '0-1' AND LOWER(black_username) = LOWER(?)) THEN 'win'"
                + " WHEN result IN ('1-0', '0-1') THEN 'loss' ELSE 'unknown' END) = LOWER(?)"
                + " AND played_at >= ?))"
                + BASE_SUFFIX);
    // Guard params, the outcome CASE's two, the outcome value, then the date bound
    assertThat(result.parameters())
        .isEqualTo(
            List.of("hikaru", "hikaru", "hikaru", "hikaru", "win", utc("2026-07-01T00:00:00Z")));
  }

  @Test
  public void testDateWithMotifCountOrderByKeepsParamOrder() {
    CompiledQuery result = compile("date >= \"2026-07-01\" ORDER BY motif_count(check) DESC");
    // The LEFT JOIN subquery param (motif name) precedes the WHERE param
    assertThat(result.parameters()).isEqualTo(List.of("CHECK", utc("2026-07-01T00:00:00Z")));
  }

  @Test
  public void testDateAndMonthRejectedInGroupBy() {
    assertThatThrownBy(
            () -> compiler.compileAggregate(Parser.parse("white.elo > 1"), List.of("date")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("not supported in groupBy");
    assertThatThrownBy(
            () -> compiler.compileAggregate(Parser.parse("white.elo > 1"), List.of("month")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("not supported in groupBy");
  }

  @Test
  public void testCompileAggregateSingleGroupBy() {
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("white.username = \"hikaru\" AND time.class = \"blitz\""),
            List.of("opening_family"));

    assertThat(result.selectSql())
        .isEqualTo(
            "SELECT opening_family, COUNT(*) AS group_count FROM game_features g"
                + " WHERE (LOWER(white_username) = LOWER(?) AND LOWER(time_class) = LOWER(?))"
                + " GROUP BY opening_family"
                + " ORDER BY group_count DESC, opening_family ASC");
    assertThat(result.parameters()).isEqualTo(List.of("hikaru", "blitz"));
  }

  @Test
  public void testCompileAggregateMultipleGroupByDedupesAndResolvesDottedFields() {
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("white.elo >= 2500"),
            List.of("opening.family", "black.title", "opening_family"));

    assertThat(result.selectSql())
        .isEqualTo(
            "SELECT opening_family, black_title, COUNT(*) AS group_count FROM game_features g"
                + " WHERE white_elo >= ?"
                + " GROUP BY opening_family, black_title"
                + " ORDER BY group_count DESC, opening_family ASC, black_title ASC");
  }

  @Test
  public void testCompileAggregateSupportsMotifPredicates() {
    CompiledQuery result =
        compiler.compileAggregate(Parser.parse("motif(fork)"), List.of("time_class"));
    assertThat(result.selectSql()).contains(FORK_EXISTS).contains("GROUP BY time_class");
  }

  @Test
  public void testCompileAggregateRejectsUnknownGroupByField() {
    assertThatThrownBy(
            () -> compiler.compileAggregate(Parser.parse("white.elo >= 2500"), List.of("pgn")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unknown field");
  }

  @Test
  public void testCompileAggregateRejectsEmptyGroupBy() {
    assertThatThrownBy(
            () -> compiler.compileAggregate(Parser.parse("white.elo >= 2500"), List.of()))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("groupBy requires at least one field");
  }

  @Test
  public void testCompileAggregateRejectsOrderByMotifCount() {
    assertThatThrownBy(
            () ->
                compiler.compileAggregate(
                    Parser.parse("motif(check) ORDER BY motif_count(checkmate) DESC"),
                    List.of("time_class")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("not supported in aggregate");
  }

  private static final String PARTICIPATION_GUARD =
      "(LOWER(white_username) = LOWER(?) OR LOWER(black_username) = LOWER(?))";

  @Test
  public void testPerspectiveOutcomeCompilesWithParticipationGuard() {
    CompiledQuery result = compiler.compile(Parser.parse("outcome = \"win\""), "hikaru");

    assertThat(result.selectSql())
        .isEqualTo(
            BASE_PREFIX
                + "("
                + PARTICIPATION_GUARD
                + " AND LOWER(CASE WHEN result = '1/2-1/2' THEN 'draw'"
                + " WHEN (result = '1-0' AND LOWER(white_username) = LOWER(?))"
                + " OR (result = '0-1' AND LOWER(black_username) = LOWER(?)) THEN 'win'"
                + " WHEN result IN ('1-0', '0-1') THEN 'loss' ELSE 'unknown' END) = LOWER(?))"
                + BASE_SUFFIX);
    // Guard params first (they appear first in the SQL), then the outcome's two, then the value
    assertThat(result.parameters())
        .isEqualTo(List.of("hikaru", "hikaru", "hikaru", "hikaru", "win"));
  }

  @Test
  public void testPerspectiveMeColorAndOpponentTitle() {
    CompiledQuery result =
        compiler.compile(
            Parser.parse("me.color = \"white\" AND opponent.title = \"GM\""), "hikaru");

    assertThat(result.selectSql())
        .contains(PARTICIPATION_GUARD)
        .contains(
            "LOWER(CASE WHEN LOWER(white_username) = LOWER(?) THEN 'white' ELSE 'black' END) ="
                + " LOWER(?)")
        .contains(
            "LOWER(CASE WHEN LOWER(white_username) = LOWER(?) THEN black_title ELSE white_title"
                + " END) = LOWER(?)");
    assertThat(result.parameters())
        .isEqualTo(List.of("hikaru", "hikaru", "hikaru", "white", "hikaru", "GM"));
  }

  @Test
  public void testPerspectiveEloIsNumericComparison() {
    CompiledQuery result = compiler.compile(Parser.parse("opponent.elo >= 2500"), "hikaru");
    assertThat(result.selectSql())
        .contains(
            "(CASE WHEN LOWER(white_username) = LOWER(?) THEN black_elo ELSE white_elo END) >= ?");
    assertThat(result.parameters()).isEqualTo(List.of("hikaru", "hikaru", "hikaru", 2500));
  }

  @Test
  public void testPerspectiveFieldSupportsIn() {
    CompiledQuery result =
        compiler.compile(Parser.parse("opponent.title IN [\"GM\", \"IM\"]"), "hikaru");
    assertThat(result.selectSql()).contains("END) IN (LOWER(?), LOWER(?))");
    assertThat(result.parameters()).isEqualTo(List.of("hikaru", "hikaru", "hikaru", "GM", "IM"));
  }

  @Test
  public void testPerspectiveWithMotifCountOrderByKeepsParamOrder() {
    CompiledQuery result =
        compiler.compile(
            Parser.parse("outcome = \"win\" ORDER BY motif_count(pin) DESC"), "hikaru");
    // LEFT JOIN subquery param (motif name) precedes the guarded WHERE params
    assertThat(result.parameters())
        .isEqualTo(List.of("PIN", "hikaru", "hikaru", "hikaru", "hikaru", "win"));
  }

  @Test
  public void testPerspectiveWithTopLevelOrGuardsWholeExpression() {
    // The participation guard must wrap the entire disjunction: a non-perspective OR-branch may
    // not leak games the player didn't participate in.
    CompiledQuery result =
        compiler.compile(Parser.parse("outcome = \"win\" OR white.elo > 2800"), "hikaru");

    assertThat(result.selectSql())
        .isEqualTo(
            BASE_PREFIX
                + "("
                + PARTICIPATION_GUARD
                + " AND (LOWER(CASE WHEN result = '1/2-1/2' THEN 'draw'"
                + " WHEN (result = '1-0' AND LOWER(white_username) = LOWER(?))"
                + " OR (result = '0-1' AND LOWER(black_username) = LOWER(?)) THEN 'win'"
                + " WHEN result IN ('1-0', '0-1') THEN 'loss' ELSE 'unknown' END) = LOWER(?)"
                + " OR white_elo > ?))"
                + BASE_SUFFIX);
    assertThat(result.parameters())
        .isEqualTo(List.of("hikaru", "hikaru", "hikaru", "hikaru", "win", 2800));
  }

  @Test
  public void testPerspectiveOutcomeSupportsIn() {
    CompiledQuery result =
        compiler.compile(Parser.parse("outcome IN [\"win\", \"draw\"]"), "hikaru");
    assertThat(result.selectSql()).contains("END) IN (LOWER(?), LOWER(?))");
    // Guard's two, the outcome CASE's two, then the IN values
    assertThat(result.parameters())
        .isEqualTo(List.of("hikaru", "hikaru", "hikaru", "hikaru", "win", "draw"));
  }

  @Test
  public void testPerspectiveFieldWithoutPlayerRejected() {
    assertThatThrownBy(() -> compile("outcome = \"win\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("requires a player");
    assertThatThrownBy(() -> compiler.compile(Parser.parse("me.elo > 2000"), "  "))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("requires a player");
  }

  @Test
  public void testNonPerspectiveQueryIgnoresPlayerParam() {
    CompiledQuery result = compiler.compile(Parser.parse("white.elo >= 2500"), "hikaru");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + "white_elo >= ?" + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of(2500));
  }

  @Test
  public void testCompileAggregateSupportsPerspectiveFilter() {
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("outcome = \"loss\" AND time.class = \"blitz\""),
            List.of("opening_family"),
            "hikaru");
    assertThat(result.selectSql())
        .contains(PARTICIPATION_GUARD)
        .contains("GROUP BY opening_family");
    assertThat(result.parameters())
        .isEqualTo(List.of("hikaru", "hikaru", "hikaru", "hikaru", "loss", "blitz"));
  }

  @Test
  public void testCompileAggregateRejectsPerspectiveGroupBy() {
    assertThatThrownBy(
            () ->
                compiler.compileAggregate(
                    Parser.parse("white.elo > 1"), List.of("opponent.title"), "hikaru"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Perspective fields are not supported in groupBy");
    assertThatThrownBy(
            () ->
                compiler.compileAggregate(
                    Parser.parse("white.elo > 1"), List.of("me.elo"), "hikaru"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Perspective fields are not supported in groupBy");
  }

  @Test
  public void testCompileAggregateGroupByMeColor() {
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("time.class = \"blitz\""), List.of("me.color"), "hikaru");

    // The CASE renders in the SELECT list (aliased to the underscore key); GROUP BY and the
    // tiebreak reference the alias so both dialects match the grouped expression.
    assertThat(result.selectSql())
        .isEqualTo(
            "SELECT (CASE WHEN LOWER(white_username) = LOWER(?) THEN 'white' ELSE 'black' END)"
                + " AS me_color, COUNT(*) AS group_count FROM game_features g WHERE"
                + " ((LOWER(white_username) = LOWER(?) OR LOWER(black_username) = LOWER(?))"
                + " AND LOWER(time_class) = LOWER(?))"
                + " GROUP BY me_color"
                + " ORDER BY group_count DESC, me_color ASC");
    // SELECT CASE param first, then the participation guard's two, then the filter value
    assertThat(result.parameters()).isEqualTo(List.of("hikaru", "hikaru", "hikaru", "blitz"));
  }

  @Test
  public void testCompileAggregateGroupByOutcome() {
    CompiledQuery result =
        compiler.compileAggregate(Parser.parse("white.elo >= 2500"), List.of("outcome"), "hikaru");

    assertThat(result.selectSql())
        .isEqualTo(
            "SELECT (CASE WHEN result = '1/2-1/2' THEN 'draw'"
                + " WHEN (result = '1-0' AND LOWER(white_username) = LOWER(?))"
                + " OR (result = '0-1' AND LOWER(black_username) = LOWER(?)) THEN 'win'"
                + " WHEN result IN ('1-0', '0-1') THEN 'loss' ELSE 'unknown' END) AS outcome,"
                + " COUNT(*) AS group_count FROM game_features g WHERE"
                + " ((LOWER(white_username) = LOWER(?) OR LOWER(black_username) = LOWER(?))"
                + " AND white_elo >= ?)"
                + " GROUP BY outcome"
                + " ORDER BY group_count DESC, outcome ASC");
    // The outcome CASE's two params, then the guard's two, then the filter value
    assertThat(result.parameters())
        .isEqualTo(List.of("hikaru", "hikaru", "hikaru", "hikaru", 2500));
  }

  @Test
  public void testCompileAggregateGroupByPerspectiveAcceptsUnderscoreFormAndDedupes() {
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("white.elo >= 2500"),
            List.of("me_color", "me.color", "opening.family"),
            "hikaru");

    assertThat(result.selectSql())
        .contains("END) AS me_color")
        .contains("GROUP BY me_color, opening_family")
        .contains("ORDER BY group_count DESC, me_color ASC, opening_family ASC");
    assertThat(result.parameters()).isEqualTo(List.of("hikaru", "hikaru", "hikaru", 2500));
  }

  @Test
  public void testCompileAggregatePerspectiveGroupByWithoutPlayerRejected() {
    assertThatThrownBy(
            () -> compiler.compileAggregate(Parser.parse("white.elo > 1"), List.of("me.color")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("requires a player");
    assertThatThrownBy(
            () ->
                compiler.compileAggregate(Parser.parse("white.elo > 1"), List.of("outcome"), "  "))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("requires a player");
  }

  @Test
  public void testResolveGroupByColumnsMapsPerspectiveFieldsToUnderscoreKeys() {
    assertThat(compiler.resolveGroupByColumns(List.of("me.color", "outcome", "opening.family")))
        .containsExactly("me_color", "outcome", "opening_family");
  }

  @Test
  public void testCompileAggregateTotals() {
    CompiledQuery result =
        compiler.compileAggregateTotals(
            Parser.parse("white.username = \"hikaru\" AND time.class = \"blitz\""),
            List.of("opening_family"));

    assertThat(result.selectSql())
        .isEqualTo(
            "SELECT COUNT(*) AS total_groups, COALESCE(SUM(group_count), 0) AS total_games FROM"
                + " (SELECT COUNT(*) AS group_count FROM game_features g WHERE"
                + " (LOWER(white_username) = LOWER(?) AND LOWER(time_class) = LOWER(?))"
                + " GROUP BY opening_family) grp");
    assertThat(result.parameters()).isEqualTo(List.of("hikaru", "blitz"));
  }

  @Test
  public void testCompileAggregateTotalsWithPerspectiveFilterAndGroupBy() {
    CompiledQuery result =
        compiler.compileAggregateTotals(
            Parser.parse("outcome = \"win\""), List.of("me.color"), "hikaru");

    // The inner query has no SELECT-list group expressions, so the me.color CASE inlines
    // directly in GROUP BY, binding its player param after the WHERE params.
    assertThat(result.selectSql())
        .isEqualTo(
            "SELECT COUNT(*) AS total_groups, COALESCE(SUM(group_count), 0) AS total_games FROM"
                + " (SELECT COUNT(*) AS group_count FROM game_features g WHERE"
                + " ("
                + PARTICIPATION_GUARD
                + " AND LOWER(CASE WHEN result = '1/2-1/2' THEN 'draw'"
                + " WHEN (result = '1-0' AND LOWER(white_username) = LOWER(?))"
                + " OR (result = '0-1' AND LOWER(black_username) = LOWER(?)) THEN 'win'"
                + " WHEN result IN ('1-0', '0-1') THEN 'loss' ELSE 'unknown' END) = LOWER(?))"
                + " GROUP BY (CASE WHEN LOWER(white_username) = LOWER(?) THEN 'white' ELSE"
                + " 'black' END)) grp");
    assertThat(result.parameters())
        .isEqualTo(List.of("hikaru", "hikaru", "hikaru", "hikaru", "win", "hikaru"));
  }

  @Test
  public void testCompileAggregateTotalsRejectsOrderByMotifCount() {
    assertThatThrownBy(
            () ->
                compiler.compileAggregateTotals(
                    Parser.parse("motif(check) ORDER BY motif_count(checkmate) DESC"),
                    List.of("time_class")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("not supported in aggregate");
  }

  private CompiledQuery compile(String input) {
    return compiler.compile(Parser.parse(input));
  }
}
