package com.muchq.games.chessql.compiler;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatCode;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.chessql.ast.OrderByClause;
import com.muchq.games.chessql.ast.SequenceExpr;
import com.muchq.games.chessql.parser.ParsedQuery;
import com.muchq.games.chessql.parser.Parser;
import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneOffset;
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

  /**
   * The boundary a date/month rewrite binds: played_at is a zone-free TIMESTAMP holding a UTC wall
   * clock, so the parameter is the {@link LocalDateTime} that instant reads as in UTC. Spelled as
   * an instant here so the expectation states the UTC intent rather than a bare wall clock.
   */
  private static LocalDateTime utc(String instant) {
    return Instant.parse(instant).atOffset(ZoneOffset.UTC).toLocalDateTime();
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

  // Error strings on these paths are the UI an LLM caller reads, so they are pinned exactly:
  // each must name the offending field, the accepted format, and the value that was rejected.

  @Test
  public void testDateRejectsMalformedValues() {
    assertThatThrownBy(() -> compile("date >= \"July 1, 2026\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("date requires an ISO date string (\"YYYY-MM-DD\"), got: July 1, 2026");
    // Zero-padding is required: "2026-7-1" is not ISO
    assertThatThrownBy(() -> compile("date = \"2026-7-1\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("date requires an ISO date string (\"YYYY-MM-DD\"), got: 2026-7-1");
    // A bare number is not a date
    assertThatThrownBy(() -> compile("date >= 20260701"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("date requires an ISO date string (\"YYYY-MM-DD\"), got: 20260701");
    // A bare month is not a date
    assertThatThrownBy(() -> compile("date >= \"2026-07\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("date requires an ISO date string (\"YYYY-MM-DD\"), got: 2026-07");
    // Well-formed but non-existent calendar days are rejected too
    assertThatThrownBy(() -> compile("date = \"2026-02-30\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("date requires an ISO date string (\"YYYY-MM-DD\"), got: 2026-02-30");
  }

  @Test
  public void testMonthRejectsMalformedValues() {
    assertThatThrownBy(() -> compile("month = \"2026-7\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("month requires a \"YYYY-MM\" string, got: 2026-7");
    assertThatThrownBy(() -> compile("month = \"2026-07-01\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("month requires a \"YYYY-MM\" string, got: 2026-07-01");
    assertThatThrownBy(() -> compile("month = \"July\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("month requires a \"YYYY-MM\" string, got: July");
    assertThatThrownBy(() -> compile("month = 202607"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("month requires a \"YYYY-MM\" string, got: 202607");
  }

  @Test
  public void testMonthRejectsNonEqualityOperatorsWithExactMessage() {
    assertThatThrownBy(() -> compile("month > \"2026-07\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("month supports only '=' (use date for range comparisons), got: >");
  }

  @Test
  public void testDateAndMonthRejectIn() {
    // The suggested alternative is field-specific: an LLM told "use comparisons" on month would
    // otherwise retry with month >= ... and hit a second error.
    assertThatThrownBy(() -> compile("date IN [\"2026-07-01\", \"2026-07-02\"]"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage(
            "date does not support IN; use comparisons instead (date >= \"2026-07-01\", or a"
                + " range like date >= \"2026-07-01\" AND date < \"2026-09-01\")");
    assertThatThrownBy(() -> compile("month IN [\"2026-06\", \"2026-07\"]"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage(
            "month does not support IN; use comparisons instead (month = \"2026-07\", or a"
                + " range like date >= \"2026-07-01\" AND date < \"2026-09-01\")");
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
        .hasMessage(
            "'date' is a filter-only field and is not supported in groupBy; use it in the query"
                + " filter instead (e.g. date >= \"2026-07-01\")");
    assertThatThrownBy(
            () -> compiler.compileAggregate(Parser.parse("white.elo > 1"), List.of("month")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage(
            "'month' is a filter-only field and is not supported in groupBy; use it in the query"
                + " filter instead (e.g. month = \"2026-07\")");
    // The totals companion validates groupBy identically, so the caller sees the same message
    assertThatThrownBy(
            () -> compiler.compileAggregateTotals(Parser.parse("white.elo > 1"), List.of("month")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage(
            "'month' is a filter-only field and is not supported in groupBy; use it in the query"
                + " filter instead (e.g. month = \"2026-07\")");
  }

  @Test
  public void testGroupByPerspectiveErrorMessagesAreExact() {
    // Bucket-width validation: a malformed, zero, negative, overflowing, or unclosed
    // width — and a width on a field that doesn't bucket — must each get an actionable message,
    // not the generic Unknown-field error. Width 0 in particular would otherwise compile into a
    // division by zero, and the unclosed "me.elo(100" must not lenient-parse into a working term.
    for (String field :
        List.of(
            "me.elo(0)",
            "opponent.elo(-100)",
            "me.elo(abc)",
            "me_elo()",
            "me.elo(99999999999)",
            "me.elo(100")) {
      assertThatThrownBy(
              () ->
                  compiler.compileAggregate(
                      Parser.parse("white.elo > 1"), List.of(field), "hikaru"))
          .isInstanceOf(IllegalArgumentException.class)
          .hasMessage(
              "Bucket width must be a positive integer: "
                  + field
                  + ". Bare me.elo / opponent.elo bucket by 100; opponent.elo(200) groups ratings"
                  + " into [2000, 2200), [2200, 2400), ...");
    }
    assertThatThrownBy(
            () ->
                compiler.compileAggregate(
                    Parser.parse("white.elo > 1"), List.of("me.color(100)"), "hikaru"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage(
            "Only the rating fields take a bucket width in groupBy: me.color(100). Bucketed:"
                + " me.elo, opponent.elo (e.g. opponent.elo(200)); groupable as-is, with a player:"
                + " me.color, me.title, opponent.title, opponent.username, outcome");
    // Two widths for one field would both alias the same group key, so the conflict is an error
    // rather than a silent first-one-wins. The bare spelling participates via its default.
    assertThatThrownBy(
            () ->
                compiler.compileAggregate(
                    Parser.parse("white.elo > 1"), List.of("me.elo(200)", "me_elo(300)"), "hikaru"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage(
            "Conflicting bucket widths for me.elo: 200 and 300 (bare me.elo / opponent.elo means"
                + " a width of 100)");
    assertThatThrownBy(
            () ->
                compiler.compileAggregate(
                    Parser.parse("white.elo > 1"),
                    List.of("opponent_elo", "opponent.elo(200)"),
                    "hikaru"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage(
            "Conflicting bucket widths for opponent.elo: 100 and 200 (bare me.elo / opponent.elo"
                + " means a width of 100)");
    // Groupable fields still require a player — same voice as the filter-side message. me.elo
    // included: bucket terms resolve before the player check, but must not bypass it.
    for (String field : List.of("me.color", "outcome", "opponent.title", "me.elo")) {
      assertThatThrownBy(
              () -> compiler.compileAggregate(Parser.parse("white.elo > 1"), List.of(field)))
          .isInstanceOf(IllegalArgumentException.class)
          .hasMessage(
              "Field '"
                  + field
                  + "' is perspective-relative (me.*, opponent.*, outcome) and requires a"
                  + " player parameter on the request");
    }
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

  /**
   * The guard exists to repair perspective resolution (the CASEs read "not white" as "black"), not
   * to scope results — so on the select path a player with no perspective field in play adds no
   * predicate at all. That stays: the rows carry usernames, so a caller who expected one player's
   * games sees strangers in the very first result.
   */
  @Test
  public void testPlayerWithoutPerspectiveFieldAddsNoParticipationGuardOnSelect() {
    CompiledQuery select = compiler.compile(Parser.parse("num.moves >= 0"), "hikaru");
    assertThat(select.selectSql()).doesNotContain("LOWER(white_username)");
    assertThat(select.parameters()).isEqualTo(List.of(0));
  }

  /**
   * The aggregate half of the same semantics was a footgun rather than a contract, so it is now
   * refused (#1313 item 14). {@code player=hikaru, group by opening_family} over a plain filter
   * aggregated the whole corpus under a heading that read as hikaru's, and — unlike the select path
   * — no column in the response revealed it. Both aggregate entry points reject it, because the
   * truncation path runs the totals query and the MCP facade calls the compiler directly rather
   * than through the controller.
   */
  @Test
  public void testAggregateRefusesAPlayerItWouldNotScopeTo() {
    assertThatThrownBy(
            () ->
                compiler.compileAggregate(
                    Parser.parse("num.moves >= 0"), List.of("opening_family"), "hikaru"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("would not scope this aggregate")
        .hasMessageContaining("hikaru")
        .hasMessageContaining("white.username");

    assertThatThrownBy(
            () ->
                compiler.compileAggregateTotals(
                    Parser.parse("num.moves >= 0"), List.of("opening_family"), "hikaru"))
        .as("the totals query runs on the truncation path and must refuse the same request")
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("would not scope this aggregate");
  }

  /**
   * The three ways an aggregate legitimately names a player — a perspective field in the filter, a
   * perspective field in the groupBy, or an explicit username filter that scopes it without any
   * perspective field at all. Each must still compile; a refusal that swallowed these would push
   * callers back to hand-written unions.
   */
  @Test
  public void testAggregateAcceptsEveryFormThatActuallyScopes() {
    assertThat(
            compiler
                .compileAggregate(Parser.parse("outcome = \"win\""), List.of("eco"), "hikaru")
                .selectSql())
        .as("perspective field in the filter")
        .contains(PARTICIPATION_GUARD);

    assertThat(
            compiler
                .compileAggregate(Parser.parse("num.moves >= 0"), List.of("me.color"), "hikaru")
                .selectSql())
        .as("perspective field in the groupBy")
        .contains(PARTICIPATION_GUARD);

    CompiledQuery explicit =
        compiler.compileAggregate(
            Parser.parse("white.username = \"hikaru\" OR black.username = \"hikaru\""),
            List.of("opening_family"),
            "hikaru");
    // No guard fires here — the filter is the caller's own, and it compiles to text identical to
    // the guard (the same coincidence the username indexes rely on), so asserting on the SQL
    // string could not tell the two apart. The params can: two from the filter, and no third and
    // fourth from a guard that never ran.
    assertThat(explicit.parameters())
        .as("the filter's own two binds, with no guard params layered on top")
        .isEqualTo(List.of("hikaru", "hikaru"));

    assertThat(
            compiler
                .compileAggregate(Parser.parse("num.moves >= 0"), List.of("opening_family"), null)
                .selectSql())
        .as("and naming no player at all is a corpus-wide aggregate, which is a real question")
        .doesNotContain("LOWER(white_username)");
  }

  /**
   * The question is not "is a username mentioned?" but "can this filter match a game the player did
   * not play?" — so shapes that name a username while still admitting strangers are refused. Each
   * would otherwise reproduce the exact failure the refusal exists to prevent: a corpus-wide (or
   * someone-else-wide) count under the named player's heading.
   */
  @Test
  public void testAggregateRefusesUsernameFiltersThatStillAdmitOtherPlayersGames() {
    // The complement of "is magnus" is everyone else — hikaru included, but hardly alone.
    assertUnscoped("time.class = \"blitz\" AND NOT white.username = \"magnus\"");
    // Negation of the player's own name is the case that matters: it names hikaru and admits
    // precisely the games that are not his. A rule that recursed through NOT would accept it.
    assertUnscoped("NOT white.username = \"hikaru\"");
    assertUnscoped("NOT (white.username = \"hikaru\" OR black.username = \"hikaru\")");
    // One unscoped branch of an OR admits the whole corpus, whatever the other branch says.
    assertUnscoped("white.username = \"hikaru\" OR time.class = \"blitz\"");
    // Only equality pins a value — "not hikaru" names him and excludes him.
    assertUnscoped("white.username != \"hikaru\"");
    assertUnscoped("white.username != \"magnus\"");
    // A list naming anyone else admits their games too.
    assertUnscoped("black.username IN [\"hikaru\", \"magnus\"]");
    // And a filter pinning a username to somebody who is not the named player scopes to them, not
    // to the player the response will be read as being about.
    assertUnscoped("white.username = \"magnus\" OR black.username = \"magnus\"");
  }

  /** The shapes that genuinely cannot match another player's game still compile. */
  @Test
  public void testAggregateAcceptsUsernameFiltersThatCannotAdmitOtherPlayersGames() {
    // AND only narrows, so one scoping conjunct suffices, at any depth.
    assertScoped("time.class = \"blitz\" AND white.username = \"hikaru\"");
    assertScoped(
        "(white.username = \"hikaru\" OR black.username = \"hikaru\") AND time.class = \"blitz\"");
    // Every branch of the OR pins the same player.
    assertScoped("white.username = \"hikaru\" OR black.username = \"hikaru\"");
    // Case-folded, matching the case-folding the compiled predicate itself does.
    assertScoped("white.username = \"HIKARU\" OR black.username = \"Hikaru\"");
    // A single-alternative IN is an equality in list clothing.
    assertScoped("black.username IN [\"hikaru\"]");
  }

  private void assertUnscoped(String query) {
    assertThatThrownBy(
            () -> compiler.compileAggregate(Parser.parse(query), List.of("eco"), "hikaru"))
        .as(query)
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("would not scope this aggregate");
  }

  private void assertScoped(String query) {
    assertThatCode(() -> compiler.compileAggregate(Parser.parse(query), List.of("eco"), "hikaru"))
        .as(query)
        .doesNotThrowAnyException();
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

  /**
   * Grouping by opponent.title resolves the opposite side's column per row, so a both-colors
   * aggregate reads black_title where the player was White and white_title where the player was
   * Black. The color-specific columns cannot express this — white_title mixes the player's own
   * title into the buckets on half the rows.
   */
  @Test
  public void testCompileAggregateGroupByOpponentTitle() {
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("time.class = \"bullet\""), List.of("opponent.title"), "hikaru");

    assertThat(result.selectSql())
        .isEqualTo(
            "SELECT (CASE WHEN LOWER(white_username) = LOWER(?) THEN black_title ELSE white_title"
                + " END) AS opponent_title, COUNT(*) AS group_count FROM game_features g WHERE"
                + " ((LOWER(white_username) = LOWER(?) OR LOWER(black_username) = LOWER(?))"
                + " AND LOWER(time_class) = LOWER(?))"
                + " GROUP BY opponent_title"
                + " ORDER BY group_count DESC, opponent_title ASC");
    assertThat(result.parameters()).isEqualTo(List.of("hikaru", "hikaru", "hikaru", "bullet"));
  }

  @Test
  public void testCompileAggregateGroupByEveryCategoricalPerspectiveField() {
    // All five categorical fields compile, in one grouping, under either spelling; the rating
    // fields group separately, in bucketed form (see the elo-bucket tests).
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("white.elo >= 2500"),
            List.of("me.color", "me_title", "opponent.username", "opponent_title", "outcome"),
            "hikaru");

    assertThat(result.selectSql())
        .contains("END) AS me_color")
        .contains("THEN white_title ELSE black_title END) AS me_title")
        .contains("THEN black_username ELSE white_username END) AS opponent_username")
        .contains("THEN black_title ELSE white_title END) AS opponent_title")
        .contains("END) AS outcome")
        .contains("GROUP BY me_color, me_title, opponent_username, opponent_title, outcome");
  }

  @Test
  public void testCompileAggregateDedupesNewFieldSpellings() {
    // Both spellings of one field are one term: one alias, one GROUP BY key. Without the dedup
    // the SQL would alias the same CASE twice and Postgres would reject the duplicate.
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("white.elo >= 2500"),
            List.of("opponent.title", "opponent_title"),
            "hikaru");

    assertThat(result.selectSql()).containsOnlyOnce("AS opponent_title");
    assertThat(result.selectSql()).contains(" GROUP BY opponent_title ORDER BY");
  }

  @Test
  public void testCompileAggregateGroupByEloBucketsDefaultWidth() {
    // Bare opponent.elo buckets by 100, keyed by the band's numeric
    // lower bound via integer arithmetic on the aliased CASE. Exact SQL because every piece is
    // load-bearing — the / 100 * 100 must wrap the CASE (not one column), the alias must carry
    // into GROUP BY and the tiebreak, and the SELECT player param must precede the WHERE params.
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("time.class = \"blitz\""), List.of("opponent.elo"), "hikaru");

    assertThat(result.selectSql())
        .isEqualTo(
            "SELECT (CASE WHEN LOWER(white_username) = LOWER(?) THEN black_elo ELSE white_elo"
                + " END) / 100 * 100 AS opponent_elo, COUNT(*) AS group_count FROM game_features g"
                + " WHERE ((LOWER(white_username) = LOWER(?) OR LOWER(black_username) = LOWER(?))"
                + " AND LOWER(time_class) = LOWER(?))"
                + " GROUP BY opponent_elo"
                + " ORDER BY group_count DESC, opponent_elo ASC");
    assertThat(result.parameters()).isEqualTo(List.of("hikaru", "hikaru", "hikaru", "blitz"));
  }

  @Test
  public void testCompileAggregateGroupByEloBucketsExplicitWidth() {
    // A parenthesized width overrides the default, under either spelling of the field.
    for (String field : List.of("me.elo(200)", "me_elo(200)")) {
      CompiledQuery result =
          compiler.compileAggregate(Parser.parse("white.elo > 1"), List.of(field), "hikaru");
      assertThat(result.selectSql())
          .as(field)
          .contains(
              "(CASE WHEN LOWER(white_username) = LOWER(?) THEN white_elo ELSE black_elo END)"
                  + " / 200 * 200 AS me_elo")
          .contains(" GROUP BY me_elo ORDER BY");
    }
  }

  @Test
  public void testBucketWidthOneIsTheAcceptedBoundary() {
    // The positive twin of the width-validation loop: 1 is the smallest legal width, a deliberate
    // per-rating opt-in (the rejection guards against *implicit* raw grouping, and the totals
    // machinery still reports burial). Pins the boundary so the guard can't drift to < 2.
    CompiledQuery result =
        compiler.compileAggregate(Parser.parse("white.elo > 1"), List.of("me.elo(1)"), "hikaru");
    assertThat(result.selectSql()).contains("END) / 1 * 1 AS me_elo");
  }

  @Test
  public void testBareEloAndExplicitDefaultWidthCompileIdentically() {
    // Bare me.elo is exactly me.elo(100) — same SQL, same params — so the two spellings of "the
    // default" can never drift apart.
    CompiledQuery bare =
        compiler.compileAggregate(Parser.parse("white.elo > 1"), List.of("me.elo"), "hikaru");
    CompiledQuery explicit =
        compiler.compileAggregate(Parser.parse("white.elo > 1"), List.of("me.elo(100)"), "hikaru");
    assertThat(bare.selectSql()).isEqualTo(explicit.selectSql());
    assertThat(bare.parameters()).isEqualTo(explicit.parameters());
  }

  @Test
  public void testCompileAggregateDedupesSameWidthBucketTerms() {
    // Equivalent bucket terms (any spelling, same effective width) are one term — one alias, one
    // GROUP BY key — like the categorical spellings dedup. Bare participates via its default.
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("white.elo > 1"), List.of("opponent.elo", "opponent_elo(100)"), "hikaru");
    assertThat(result.selectSql()).containsOnlyOnce("AS opponent_elo");
    assertThat(result.selectSql()).contains(" GROUP BY opponent_elo ORDER BY");

    // Width 200 sits above the Integer autobox cache (-128..127), so this dedup only holds if
    // widths compare by value — the bare/default pair above would pass under reference equality
    // by accident, because both 100s are the same cached Integer.
    CompiledQuery aboveCache =
        compiler.compileAggregate(
            Parser.parse("white.elo > 1"),
            List.of("opponent.elo(200)", "opponent_elo(200)"),
            "hikaru");
    assertThat(aboveCache.selectSql()).containsOnlyOnce("AS opponent_elo");
  }

  @Test
  public void testCompileAggregateGroupsBothRatingFieldsWithIndependentWidths() {
    // The cross-tab the buckets exist for — my band × opponent band — and the pin that the
    // conflicting-width check is scoped per group key: different widths on *different* fields are
    // two independent terms, not a conflict.
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("white.elo > 1"), List.of("me.elo", "opponent.elo(200)"), "hikaru");

    assertThat(result.selectSql())
        .contains("THEN white_elo ELSE black_elo END) / 100 * 100 AS me_elo")
        .contains("THEN black_elo ELSE white_elo END) / 200 * 200 AS opponent_elo")
        .contains("GROUP BY me_elo, opponent_elo")
        .contains("ORDER BY group_count DESC, me_elo ASC, opponent_elo ASC");
    // One player param per bucket CASE in SELECT order, then the two participation-guard params.
    assertThat(result.parameters()).isEqualTo(List.of("hikaru", "hikaru", "hikaru", "hikaru", 1));
  }

  @Test
  public void testCompileAggregateMixesBucketCategoricalAndPhysicalTerms() {
    // A bucket term composes with a categorical perspective term and a physical column in one
    // grouping; the physical column contributes no alias and no params.
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("white.elo > 1"),
            List.of("opponent.elo(200)", "me.color", "time_class"),
            "hikaru");
    assertThat(result.selectSql())
        .contains("/ 200 * 200 AS opponent_elo")
        .contains("END) AS me_color")
        .contains("GROUP BY opponent_elo, me_color, time_class")
        .contains("ORDER BY group_count DESC, opponent_elo ASC, me_color ASC, time_class ASC");
  }

  @Test
  public void testCompileAggregateTotalsBucketsRatingFields() {
    // Both aggregate paths resolve terms through resolveGroupByTerms: the totals path inlines the
    // same bucket arithmetic in GROUP BY, binding its player param after the WHERE params.
    CompiledQuery result =
        compiler.compileAggregateTotals(
            Parser.parse("time.class = \"bullet\""), List.of("opponent.elo(200)"), "hikaru");

    assertThat(result.selectSql())
        .contains(
            "GROUP BY (CASE WHEN LOWER(white_username) = LOWER(?) THEN black_elo ELSE white_elo"
                + " END) / 200 * 200) grp");
    assertThat(result.parameters()).isEqualTo(List.of("hikaru", "hikaru", "bullet", "hikaru"));
  }

  @Test
  public void testCompileAggregateTotalsGroupByOpponentTitle() {
    CompiledQuery result =
        compiler.compileAggregateTotals(
            Parser.parse("time.class = \"bullet\""), List.of("opponent.title"), "hikaru");

    // The inner query inlines the CASE in GROUP BY; its player param binds after the WHERE params.
    assertThat(result.selectSql())
        .contains(
            "GROUP BY (CASE WHEN LOWER(white_username) = LOWER(?) THEN black_title ELSE"
                + " white_title END)");
    assertThat(result.parameters()).isEqualTo(List.of("hikaru", "hikaru", "bullet", "hikaru"));
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

  /**
   * The property, not examples: every groupable perspective field resolves under both its dotted
   * and its underscore spelling, to the underscore key the response is keyed by. CHESSQL.md
   * promises the round trip (send back the key you got), so this guards the spelling derivation and
   * the field roster end to end.
   */
  @Test
  public void testEveryGroupableFieldResolvesUnderBothSpellings() {
    for (String field :
        List.of(
            "me.color",
            "me.title",
            "me.elo",
            "opponent.username",
            "opponent.title",
            "opponent.elo",
            "outcome")) {
      String key = field.replace('.', '_');
      assertThat(compiler.resolveGroupByColumns(List.of(field)))
          .as("dotted spelling %s", field)
          .containsExactly(key);
      assertThat(compiler.resolveGroupByColumns(List.of(key)))
          .as("underscore spelling %s", key)
          .containsExactly(key);
    }
    // A width never leaks into the key: the DAO reads result-set columns by these names.
    assertThat(compiler.resolveGroupByColumns(List.of("opponent.elo(200)")))
        .containsExactly("opponent_elo");
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

  /**
   * compileAggregate and compileAggregateTotals must describe the same filter and the same
   * grouping, so any divergence in what they bind is a silently-wrong totals bug (no error, just a
   * different denominator than the groups). The two lay their placeholders out differently by
   * necessity — the groups query renders the CASE in the SELECT list (params first), the totals
   * query inlines it in GROUP BY (params last) — so the invariant is that each list matches its own
   * SQL and the two agree as multisets.
   */
  @Test
  public void testAggregateAndTotalsBindTheSameParamsForTheSameQuery() {
    ParsedQuery parsed =
        Parser.parse("outcome = \"win\" AND date >= \"2026-07-01\" AND month = \"2026-07\"");
    List<String> groupBy = List.of("me.color", "opening.family");

    CompiledQuery groups = compiler.compileAggregate(parsed, groupBy, "hikaru");
    CompiledQuery totals = compiler.compileAggregateTotals(parsed, groupBy, "hikaru");

    LocalDateTime julyStart = utc("2026-07-01T00:00:00Z");
    LocalDateTime augStart = utc("2026-08-01T00:00:00Z");
    // groups: me.color CASE (1), guard (2), outcome CASE (2), "win", date bound, month bounds
    assertThat(groups.parameters())
        .isEqualTo(
            List.of(
                "hikaru", "hikaru", "hikaru", "hikaru", "hikaru", "win", julyStart, julyStart,
                augStart));
    // totals: same WHERE params in the same order, with the me.color CASE param moved to the end
    assertThat(totals.parameters())
        .isEqualTo(
            List.of(
                "hikaru", "hikaru", "hikaru", "hikaru", "win", julyStart, julyStart, augStart,
                "hikaru"));
    assertThat(groups.parameters())
        .containsExactlyInAnyOrderElementsOf(totals.parameters())
        .hasSameSizeAs(totals.parameters());
    // Every placeholder in each statement is accounted for by exactly one bound param.
    assertThat(countPlaceholders(groups.selectSql())).isEqualTo(groups.parameters().size());
    assertThat(countPlaceholders(totals.selectSql())).isEqualTo(totals.parameters().size());
  }

  /**
   * The ORDER BY motif_count LEFT JOIN binds the motif name before the WHERE clause, and the
   * participation guard prepends two player params to the WHERE clause. Both are prefixes, so a
   * query using all three plus a date bound must still line up placeholder-for-param.
   */
  @Test
  public void testMotifCountOrderByWithPerspectiveAndDateKeepsParamOrder() {
    CompiledQuery result =
        compiler.compile(
            Parser.parse(
                "outcome = \"win\" AND date >= \"2026-07-01\" ORDER BY motif_count(pin) DESC"),
            "hikaru");

    // motif name (JOIN), guard x2, outcome CASE x2, the outcome value, then the date bound
    assertThat(result.parameters())
        .isEqualTo(
            List.of(
                "PIN", "hikaru", "hikaru", "hikaru", "hikaru", "win", utc("2026-07-01T00:00:00Z")));
    assertThat(countPlaceholders(result.selectSql())).isEqualTo(result.parameters().size());
  }

  private static int countPlaceholders(String sql) {
    return (int) sql.chars().filter(c -> c == '?').count();
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
