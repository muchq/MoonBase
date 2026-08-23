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

  /** The outcome CASE as it renders inside each metric SUM — two player params per rendering. */
  private static final String OUTCOME_CASE =
      "(CASE WHEN result = '1/2-1/2' THEN 'draw'"
          + " WHEN (result = '1-0' AND LOWER(white_username) = LOWER(?))"
          + " OR (result = '0-1' AND LOWER(black_username) = LOWER(?)) THEN 'win'"
          + " WHEN result IN ('1-0', '0-1') THEN 'loss' ELSE 'unknown' END)";

  /**
   * The per-group outcome metrics every player-scoped aggregate carries after {@code group_count}.
   * Named once here so the exact-SQL assertions below stay about the thing each of them is pinning
   * — the metrics themselves are pinned by {@code testCompileAggregateAddsOutcomeMetrics}.
   */
  private static final String OUTCOME_METRICS =
      ", SUM(CASE WHEN "
          + OUTCOME_CASE
          + " = 'win' THEN 1 ELSE 0 END) AS wins"
          + ", SUM(CASE WHEN "
          + OUTCOME_CASE
          + " = 'loss' THEN 1 ELSE 0 END) AS losses"
          + ", SUM(CASE WHEN "
          + OUTCOME_CASE
          + " = 'draw' THEN 1 ELSE 0 END) AS draws";

  /** The six player binds the metric block adds, in SELECT-list position. */
  private static List<Object> metricParams(String player) {
    return List.copyOf(java.util.Collections.nCopies(6, player));
  }

  @SafeVarargs
  private static List<Object> params(List<Object>... groups) {
    List<Object> all = new java.util.ArrayList<>();
    for (List<Object> group : groups) {
      all.addAll(group);
    }
    return all;
  }

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

  /**
   * A motif is an EXISTS, which is two-valued already — but it negates through the same {@code IS
   * NOT TRUE} as everything else. One negation idiom, no per-predicate table of which subexpression
   * can be NULL for a later reader to consult and get wrong.
   */
  @Test
  public void testNotExpression() {
    CompiledQuery result = compile("NOT motif(pin)");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "(" + motifExists("PIN") + ") IS NOT TRUE" + BASE_SUFFIX);
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
        .isEqualTo(BASE_PREFIX + "(LOWER(black_title) = LOWER(?)) IS NOT TRUE" + BASE_SUFFIX);
    assertThat(black.parameters()).isEqualTo(List.of("GM"));
  }

  @Test
  public void testTitleFieldSupportsIn() {
    CompiledQuery result = compile("black.title IN [\"GM\", \"IM\"]");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "LOWER(black_title) IN (LOWER(?), LOWER(?))" + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of("GM", "IM"));
  }

  /**
   * #1302. Every filterable column but game_url and platform is nullable, and SQL's NOT is
   * three-valued: {@code NOT NULL} is NULL, WHERE drops a NULL row, so a bare negation deleted
   * precisely the rows the caller wrote it to catch. {@code opponent.title != "GM"} dropped every
   * untitled opponent and returned a plausible smaller number.
   *
   * <p>{@code IS NOT TRUE} folds NULL to TRUE at the negation boundary, which is what a negated
   * filter means: a value nobody knows is not the value you named.
   */
  @Test
  public void testNegationKeepsNullRows() {
    String negatedTitle = BASE_PREFIX + "(LOWER(black_title) = LOWER(?)) IS NOT TRUE" + BASE_SUFFIX;
    assertThat(compile("black.title != \"GM\"").selectSql()).isEqualTo(negatedTitle);
    assertThat(compile("NOT black.title = \"GM\"").selectSql())
        .as("the two spellings are the same question and must compile alike")
        .isEqualTo(negatedTitle);

    assertThat(compile("NOT black.title IN [\"GM\", \"IM\"]").selectSql())
        .isEqualTo(
            BASE_PREFIX + "(LOWER(black_title) IN (LOWER(?), LOWER(?))) IS NOT TRUE" + BASE_SUFFIX);

    // Not a string-column special case: num.moves is nullable too, and so is every rating.
    assertThat(compile("num.moves != 40").selectSql())
        .isEqualTo(BASE_PREFIX + "(num_moves = ?) IS NOT TRUE" + BASE_SUFFIX);
  }

  /**
   * The control for the test above: only negation changes. A positive comparison still excludes
   * NULLs, which is what {@code title = "GM"} means, and the ordering operators are not negations —
   * an unknown rating is not "below 2500", it is unknown.
   */
  @Test
  public void testPositiveComparisonsStillExcludeNullRows() {
    assertThat(compile("black.title = \"GM\"").selectSql())
        .isEqualTo(BASE_PREFIX + "LOWER(black_title) = LOWER(?)" + BASE_SUFFIX);
    assertThat(compile("black.title IN [\"GM\"]").selectSql())
        .isEqualTo(BASE_PREFIX + "LOWER(black_title) IN (LOWER(?))" + BASE_SUFFIX);
    assertThat(compile("white.elo < 2500").selectSql())
        .isEqualTo(BASE_PREFIX + "white_elo < ?" + BASE_SUFFIX);
  }

  /**
   * Negation stays its own inverse. {@code IS NOT TRUE} always yields TRUE or FALSE, never NULL, so
   * the outer negation of a doubled one is an ordinary two-valued flip — {@code NOT NOT X} selects
   * the same rows as {@code X}, including dropping the NULL ones. A rewrite that folded NULL to
   * TRUE on the *inside* of the negation instead would quietly break this.
   */
  @Test
  public void testDoubleNegationSelectsTheSameRowsAsThePositive() {
    assertThat(compile("NOT NOT black.title = \"GM\"").selectSql())
        .isEqualTo(
            BASE_PREFIX
                + "((LOWER(black_title) = LOWER(?)) IS NOT TRUE) IS NOT TRUE"
                + BASE_SUFFIX);
    assertThat(compile("NOT black.title != \"GM\"").selectSql())
        .as("NOT (x != v) is the double negation spelled the other way")
        .isEqualTo(compile("NOT NOT black.title = \"GM\"").selectSql());
  }

  /**
   * The perspective path is the one the issue was filed from: {@code opponent.title} resolves to a
   * CASE over both title columns, so the negation wraps the whole CASE. Wrapping rather than
   * duplicating it into an {@code IS NULL OR ...} is load-bearing — the CASE carries a player bind
   * param, and a second rendering would need a second param in the right position.
   */
  @Test
  public void testNegatedPerspectiveTitleKeepsUntitledOpponents() {
    CompiledQuery result = compiler.compile(Parser.parse("opponent.title != \"GM\""), "hikaru");

    assertThat(result.selectSql())
        .isEqualTo(
            BASE_PREFIX
                + "("
                + PARTICIPATION_GUARD
                + " AND (LOWER(CASE WHEN LOWER(white_username) = LOWER(?) THEN black_title"
                + " ELSE white_title END) = LOWER(?)) IS NOT TRUE)"
                + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of("hikaru", "hikaru", "hikaru", "GM"));
  }

  /**
   * played_at is nullable too, so the date fields carry the same defect (#1256's first half) and
   * take the same cure. Worth pinning separately because {@code date != "D"} is not compiled as a
   * bare {@code !=}: a day is an interval, so the negation is of the whole half-open range rather
   * than of either boundary.
   */
  @Test
  public void testNegatedDateKeepsRowsWithNoPlayedAt() {
    CompiledQuery result = compile("date != \"2026-07-01\"");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "(played_at >= ? AND played_at < ?) IS NOT TRUE" + BASE_SUFFIX);
    assertThat(result.parameters())
        .isEqualTo(List.of(utc("2026-07-01T00:00:00Z"), utc("2026-07-02T00:00:00Z")));

    // The other spelling differs by one redundant pair of parens and nothing else: the day range
    // arrives at the negation already wrapped, and negate() does not inspect what it is handed.
    // That the two select the same rows is pinned where it can actually be observed, in
    // GameFeatureDaoTest.dateOperators_boundaryInstantsOnH2.
    assertThat(compile("NOT date = \"2026-07-01\"").selectSql())
        .isEqualTo(BASE_PREFIX + "((played_at >= ? AND played_at < ?)) IS NOT TRUE" + BASE_SUFFIX);
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
  public void testMonthEqualityCompilesToMonthRange() {
    CompiledQuery result = compile("month = \"2026-07\"");
    assertThat(result.selectSql())
        .isEqualTo(BASE_PREFIX + "(played_at >= ? AND played_at < ?)" + BASE_SUFFIX);
    assertThat(result.parameters())
        .isEqualTo(List.of(utc("2026-07-01T00:00:00Z"), utc("2026-08-01T00:00:00Z")));
  }

  // === played.at (direct timestamp comparisons) ===

  @Test
  public void testPlayedAtBindsAFullTimestampAsLocalDateTime() {
    CompiledQuery result = compile("played.at >= \"2026-07-01T13:30:00\"");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + "played_at >= ?" + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of(utc("2026-07-01T13:30:00Z")));
  }

  @Test
  public void testPlayedAtUnderscoreFormBindsTheSameWay() {
    CompiledQuery result = compile("played_at < \"2026-07-01T00:00:00\"");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + "played_at < ?" + BASE_SUFFIX);
    assertThat(result.parameters()).isEqualTo(List.of(utc("2026-07-01T00:00:00Z")));
  }

  @Test
  public void testPlayedAtRejectsABareDatePointingAtDateAndMonth() {
    // The likeliest near-miss: a bare date bound raw would either compare as midnight (matching
    // almost nothing while looking like "that day") or, on Postgres, fail the bind outright.
    // date/month are the fields that mean "that day"/"that month", so the error hands them over.
    assertThatThrownBy(() -> compile("played.at >= \"2026-07-01\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage(
            "played.at requires a full ISO timestamp (\"YYYY-MM-DDTHH:MM:SS\", the stored UTC"
                + " wall clock; a trailing Z or offset is converted), got: 2026-07-01. To filter"
                + " by day or month use date or month instead, e.g. date >= \"2026-07-01\" or"
                + " month = \"2026-07\"");
  }

  @Test
  public void testPlayedAtAcceptsZuluAndOffsetTimestampsAsUtc() {
    // "...T13:30:00Z" is the likeliest shape an MCP/LLM caller writes. The convention is a UTC
    // wall clock, so a zone-qualified instant has exactly one correct reading — take it.
    CompiledQuery zulu = compile("played.at >= \"2026-07-01T13:30:00Z\"");
    assertThat(zulu.parameters()).isEqualTo(List.of(utc("2026-07-01T13:30:00Z")));

    CompiledQuery offset = compile("played.at >= \"2026-07-01T18:30:00+05:00\"");
    assertThat(offset.parameters()).isEqualTo(List.of(utc("2026-07-01T13:30:00Z")));
  }

  @Test
  public void testPlayedAtTruncatesToMicroseconds() {
    // Both engines store microsecond TIMESTAMPs, but only pgjdbc rounds the parameter; H2 would
    // compare full nanoseconds. Truncating at compile time keeps the two engines agreeing.
    CompiledQuery result = compile("played.at = \"2026-07-01T13:30:00.123456789\"");
    assertThat(result.parameters())
        .isEqualTo(List.of(LocalDateTime.of(2026, 7, 1, 13, 30, 0, 123_456_000)));
  }

  @Test
  public void testDateScopingFieldsRejectAbsurdYears() {
    // LocalDate/LocalDateTime accept year ±999,999,999; Postgres timestamps stop at 294276 AD.
    // Reject at compile time so the caller gets a 400 with the format, not an engine-dependent
    // 500 (or a silently wrapped comparison).
    assertThatThrownBy(() -> compile("played.at > \"+300000000-01-01T00:00\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("played.at requires a full ISO timestamp");
    assertThatThrownBy(() -> compile("date >= \"+300000000-01-01\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("date requires an ISO date string (\"YYYY-MM-DD\"), got: +300000000-01-01");
    assertThatThrownBy(() -> compile("month = \"+300000000-01\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("month requires a \"YYYY-MM\" string, got: +300000000-01");
  }

  // === value/column type coercion ===
  // JDBI binds a String parameter as VARCHAR; H2 coerces it against a typed column, Postgres
  // rejects it at the operator — a 500 in production for a green local test. So a mistyped value
  // is rejected at compile time, with the fix in the message.

  @Test
  public void testQuotedNumberAgainstAnIntColumnIsRejectedWithTheFix() {
    assertThatThrownBy(() -> compile("white.elo >= \"2500\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("white.elo takes a number, got: \"2500\" — drop the quotes");
  }

  @Test
  public void testNonNumericStringAgainstAnIntColumnGetsNoDropTheQuotesHint() {
    // The negative twin: "drop the quotes" for white.elo >= "GM" would advise the edit that
    // produces the unquoted-identifier error, which advises quoting it again. When the string is
    // not a number, name the type and stop.
    assertThatThrownBy(() -> compile("white.elo >= \"GM\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("white.elo takes a number, got: \"GM\"");
  }

  @Test
  public void testBareNumberAgainstAStringColumnIsRejectedWithTheFix() {
    assertThatThrownBy(() -> compile("eco = 90"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("eco takes a double-quoted string, got: 90 — add quotes: \"90\"");
  }

  @Test
  public void testInListValuesAreCoercedPerColumnType() {
    assertThatThrownBy(() -> compile("white_elo IN [\"2500\"]"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("white_elo takes a number");
    assertThatThrownBy(() -> compile("platform IN [5]"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("platform takes a double-quoted string");
  }

  @Test
  public void testPerspectiveValuesAreCoercedByKind() {
    assertThatThrownBy(() -> compiler.compile(Parser.parse("me.elo >= \"2500\""), "hikaru"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("me.elo takes a number");
    assertThatThrownBy(() -> compiler.compile(Parser.parse("me.color = 5"), "hikaru"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("me.color takes a double-quoted string");
    assertThatThrownBy(() -> compiler.compile(Parser.parse("outcome IN [5]"), "hikaru"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("outcome takes a double-quoted string");
  }

  // === played.at parameter position on the re-ordering paths ===

  @Test
  public void testPlayedAtParamStaysAfterTheMotifCountParam() {
    // The ORDER BY motif_count path prepends the motif param ahead of the WHERE params — the one
    // select-path re-ordering where a misplaced timestamp would silently bind to the wrong slot.
    CompiledQuery result =
        compile("played.at >= \"2026-07-01T00:00:00\" ORDER BY motif_count(pin) DESC");
    assertThat(result.parameters()).isEqualTo(List.of("PIN", utc("2026-07-01T00:00:00Z")));
  }

  @Test
  public void testPlayedAtFilterCompilesThroughTheAggregatePath() {
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("played.at >= \"2026-07-01T00:00:00\""), List.of("eco"));
    assertThat(result.parameters()).isEqualTo(List.of(utc("2026-07-01T00:00:00Z")));
    assertThat(result.selectSql()).contains("GROUP BY eco");
  }

  // === unknown-name rejections enumerate the vocabulary ===

  @Test
  public void testUnknownFieldNamesTheRoster() {
    assertThatThrownBy(() -> compile("played = \"x\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unknown field: played")
        .hasMessageContaining("Known fields: ")
        .hasMessageContaining("played.at")
        .hasMessageContaining("month")
        .hasMessageContaining("with a player: ")
        .hasMessageContaining("outcome");
  }

  @Test
  public void testUnknownMotifNamesTheRosterEverywhereMotifsAreNamed() {
    assertThatThrownBy(() -> compile("motif(windmill)"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unknown motif: windmill")
        .hasMessageContaining("Known motifs: ")
        .hasMessageContaining("smothered_mate");
    assertThatThrownBy(() -> compile("motif(pin) ORDER BY motif_count(windmill)"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Known motifs: ");
    assertThatThrownBy(() -> compile("sequence(pin THEN windmill)"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Known motifs: ");
  }

  @Test
  public void testPlayedAtRejectsNumbers() {
    assertThatThrownBy(() -> compile("played.at > 2026"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("played.at requires a full ISO timestamp")
        .hasMessageContaining("got: 2026");
  }

  @Test
  public void testPlayedAtErrorNamesTheSpellingTheCallerWrote() {
    assertThatThrownBy(() -> compile("played_at = \"yesterday\""))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("played_at requires a full ISO timestamp");
  }

  @Test
  public void testPlayedAtInListConvertsEveryValue() {
    CompiledQuery result =
        compile("played.at IN [\"2026-07-01T13:30:00\", \"2026-07-02T09:00:00\"]");
    assertThat(result.selectSql()).isEqualTo(BASE_PREFIX + "played_at IN (?, ?)" + BASE_SUFFIX);
    assertThat(result.parameters())
        .isEqualTo(List.of(utc("2026-07-01T13:30:00Z"), utc("2026-07-02T09:00:00Z")));
  }

  @Test
  public void testPlayedAtInListRejectsBareDates() {
    assertThatThrownBy(() -> compile("played.at IN [\"2026-07-01\"]"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("played.at requires a full ISO timestamp");
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
        .isEqualTo(
            params(
                metricParams("hikaru"),
                List.of("hikaru", "hikaru", "hikaru", "hikaru", "loss", "blitz")));
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
        .as("the metric block's binds, then the filter's own two, with no guard params on top")
        .isEqualTo(params(metricParams("hikaru"), List.of("hikaru", "hikaru")));

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
                + " AS me_color, COUNT(*) AS group_count"
                + OUTCOME_METRICS
                + " FROM game_features g WHERE"
                + " ((LOWER(white_username) = LOWER(?) OR LOWER(black_username) = LOWER(?))"
                + " AND LOWER(time_class) = LOWER(?))"
                + " GROUP BY me_color"
                + " ORDER BY group_count DESC, me_color ASC");
    // SELECT CASE param first, then the metric block's, then the participation guard's two, then
    // the filter value — textual order, which is the order the driver binds them in
    assertThat(result.parameters())
        .isEqualTo(
            params(
                List.of("hikaru"), metricParams("hikaru"), List.of("hikaru", "hikaru", "blitz")));
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
                + " COUNT(*) AS group_count"
                + OUTCOME_METRICS
                + " FROM game_features g WHERE"
                + " ((LOWER(white_username) = LOWER(?) OR LOWER(black_username) = LOWER(?))"
                + " AND white_elo >= ?)"
                + " GROUP BY outcome"
                + " ORDER BY group_count DESC, outcome ASC");
    // The outcome CASE's two params, the metric block's, then the guard's two, then the filter
    assertThat(result.parameters())
        .isEqualTo(
            params(
                List.of("hikaru", "hikaru"),
                metricParams("hikaru"),
                List.of("hikaru", "hikaru", 2500)));
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
    assertThat(result.parameters())
        .isEqualTo(
            params(List.of("hikaru"), metricParams("hikaru"), List.of("hikaru", "hikaru", 2500)));
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
                + " END) AS opponent_title, COUNT(*) AS group_count"
                + OUTCOME_METRICS
                + " FROM game_features g WHERE"
                + " ((LOWER(white_username) = LOWER(?) OR LOWER(black_username) = LOWER(?))"
                + " AND LOWER(time_class) = LOWER(?))"
                + " GROUP BY opponent_title"
                + " ORDER BY group_count DESC, opponent_title ASC");
    assertThat(result.parameters())
        .isEqualTo(
            params(
                List.of("hikaru"), metricParams("hikaru"), List.of("hikaru", "hikaru", "bullet")));
  }

  /**
   * The metric block itself, pinned once so the exact-SQL tests above can name it. Each SUM
   * re-renders the same {@code outcome} CASE the group key would use, which is the point: a result
   * string the compiler classifies one way in a group must be classified the same way in a metric.
   */
  @Test
  public void testCompileAggregateAddsOutcomeMetricsForAPlayer() {
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("outcome = \"win\""), List.of("opening_family"), "hikaru");

    assertThat(result.selectSql())
        .contains("COUNT(*) AS group_count" + OUTCOME_METRICS + " FROM game_features g");
    // Six player binds, in SELECT-list position: two per metric CASE rendering. Score is not
    // among them — it is derived from wins and draws rather than summed again (#1370).
    assertThat(result.parameters().subList(0, 6)).isEqualTo(metricParams("hikaru"));
    assertThat(result.selectSql()).doesNotContain("score_points");
  }

  /**
   * The negative half: without a player there is no side to attribute a result to, so no metric is
   * emitted at all — not a column of zeroes, which would read as "never won these" rather than
   * "nobody asked". The twin above shares the compiler and the group-by, so a broken fixture cannot
   * masquerade as this holding.
   */
  @Test
  public void testCompileAggregateOmitsOutcomeMetricsWithoutAPlayer() {
    CompiledQuery result =
        compiler.compileAggregate(Parser.parse("white.elo >= 2500"), List.of("opening_family"));

    assertThat(result.selectSql())
        .contains("COUNT(*) AS group_count FROM game_features g")
        .doesNotContain("AS wins")
        .doesNotContain("AS losses")
        .doesNotContain("AS draws")
        .doesNotContain("AS score_points");
    assertThat(result.parameters()).isEqualTo(List.of(2500));
  }

  /**
   * Ordering by score ranks before the limit truncates, which is the only place it can happen —
   * that is what makes it a server-side parameter rather than a client-side sort. It ranks by
   * points per game, so the grouped query becomes a derived table the outer ORDER BY can divide
   * across (neither dialect resolves SELECT aliases inside an ORDER BY expression), and game count
   * breaks ties so a 1-game 100% group cannot outrank a 40-game one at the same rate.
   */
  @Test
  public void testCompileAggregateOrdersByScoreWhenAsked() {
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("outcome = \"win\""),
            new AggregateSpec(List.of("opening_family"), "hikaru", AggregateSpec.Order.SCORE, 0));

    assertThat(result.selectSql())
        .startsWith("SELECT * FROM (SELECT opening_family, COUNT(*) AS group_count")
        .endsWith(
            ") agg ORDER BY (wins * 2 + draws) * 1.0 / group_count DESC, group_count DESC,"
                + " opening_family ASC");
    // The limit the DAO appends applies to the ranked result, not to an inner slice of it.
    assertThat(result.selectSql().indexOf(") agg ORDER BY"))
        .isGreaterThan(result.selectSql().indexOf("GROUP BY opening_family"));
  }

  /** The default is unchanged, and says so here rather than only by the absence of a failure. */
  @Test
  public void testCompileAggregateOrdersByCountByDefault() {
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("outcome = \"win\""),
            new AggregateSpec(List.of("opening_family"), "hikaru", null, 0));

    assertThat(result.selectSql())
        .contains(" ORDER BY group_count DESC, opening_family ASC")
        .doesNotContain("wins * 2 + draws");
  }

  /**
   * The floor is a HAVING on the group's own count, and it binds after the WHERE clause — the DAO
   * appends {@code LIMIT ?} after everything the compiler produced, so a floor param out of
   * position would silently become the limit.
   */
  @Test
  public void testCompileAggregateAppliesTheMinimumGamesFloor() {
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("white.elo >= 2500"),
            new AggregateSpec(List.of("opening_family"), null, AggregateSpec.Order.COUNT, 5));

    assertThat(result.selectSql())
        .contains(" GROUP BY opening_family HAVING COUNT(*) >= ? ORDER BY group_count DESC");
    assertThat(result.parameters()).isEqualTo(List.of(2500, 5));
    assertThat(countPlaceholders(result.selectSql())).isEqualTo(result.parameters().size());
  }

  /** No floor asked for, no HAVING clause — and so no bind the LIMIT could be mistaken for. */
  @Test
  public void testCompileAggregateWithoutAFloorHasNoHavingClause() {
    CompiledQuery result =
        compiler.compileAggregate(
            Parser.parse("white.elo >= 2500"),
            new AggregateSpec(List.of("opening_family"), null, AggregateSpec.Order.COUNT, 0));

    assertThat(result.selectSql()).doesNotContain("HAVING");
    assertThat(result.parameters()).isEqualTo(List.of(2500));
  }

  /**
   * The totals query has to apply the same floor. Without it {@code totalGroups} counts groups the
   * caller asked not to see, so {@code truncated} reports a tail that was never on offer — the same
   * class of silently-wrong denominator the two queries were reconciled for in the first place.
   */
  @Test
  public void testCompileAggregateTotalsAppliesTheSameFloor() {
    AggregateSpec spec =
        new AggregateSpec(List.of("opening_family"), null, AggregateSpec.Order.COUNT, 5);
    CompiledQuery totals = compiler.compileAggregateTotals(Parser.parse("white.elo >= 2500"), spec);

    assertThat(totals.selectSql()).contains(" GROUP BY opening_family HAVING COUNT(*) >= ?) grp");
    assertThat(totals.parameters()).isEqualTo(List.of(2500, 5));
    assertThat(countPlaceholders(totals.selectSql())).isEqualTo(totals.parameters().size());

    CompiledQuery unfloored =
        compiler.compileAggregateTotals(
            Parser.parse("white.elo >= 2500"),
            new AggregateSpec(List.of("opening_family"), null, AggregateSpec.Order.COUNT, 0));
    assertThat(unfloored.selectSql()).doesNotContain("HAVING");
  }

  /**
   * The metrics render the outcome CASE without marking the perspective used, so they cannot
   * satisfy the rule that refuses a player the aggregate would not scope to. If they did, {@code
   * player=hikaru, num.moves >= 0, group by opening_family} would start being answered — with a
   * participation guard nobody asked for, over a corpus the caller believed was already theirs.
   */
  @Test
  public void testOutcomeMetricsDoNotSatisfyTheScopingRule() {
    assertThatThrownBy(
            () ->
                compiler.compileAggregate(
                    Parser.parse("num.moves >= 0"),
                    new AggregateSpec(
                        List.of("opening_family"), "hikaru", AggregateSpec.Order.SCORE, 10)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("would not scope this aggregate");
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
                + " END) / 100 * 100 AS opponent_elo, COUNT(*) AS group_count"
                + OUTCOME_METRICS
                + " FROM game_features g"
                + " WHERE ((LOWER(white_username) = LOWER(?) OR LOWER(black_username) = LOWER(?))"
                + " AND LOWER(time_class) = LOWER(?))"
                + " GROUP BY opponent_elo"
                + " ORDER BY group_count DESC, opponent_elo ASC");
    assertThat(result.parameters())
        .isEqualTo(
            params(
                List.of("hikaru"), metricParams("hikaru"), List.of("hikaru", "hikaru", "blitz")));
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
    // One player param per bucket CASE in SELECT order, then the metric block, then the two
    // participation-guard params.
    assertThat(result.parameters())
        .isEqualTo(
            params(
                List.of("hikaru", "hikaru"),
                metricParams("hikaru"),
                List.of("hikaru", "hikaru", 1)));
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
    // groups: me.color CASE (1), the metric block (10), guard (2), outcome CASE (2), "win", date
    // bound, month bounds
    assertThat(groups.parameters())
        .isEqualTo(
            params(
                List.of("hikaru"),
                metricParams("hikaru"),
                List.of(
                    "hikaru", "hikaru", "hikaru", "hikaru", "win", julyStart, julyStart,
                    augStart)));
    // totals: same WHERE params in the same order, with the me.color CASE param moved to the end
    // and no metric block — the totals query counts groups and games, which the metrics do not
    // change
    assertThat(totals.parameters())
        .isEqualTo(
            List.of(
                "hikaru", "hikaru", "hikaru", "hikaru", "win", julyStart, julyStart, augStart,
                "hikaru"));
    // Filter and grouping agree once the metric block is set aside: it sits between the group
    // expression's params and the WHERE clause's, so dropping that slice leaves exactly what the
    // totals query binds.
    List<Object> groupsWithoutMetrics = new java.util.ArrayList<>(groups.parameters());
    groupsWithoutMetrics.subList(1, 1 + metricParams("hikaru").size()).clear();
    assertThat(groupsWithoutMetrics)
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

  /**
   * {@code filterableFields()} is a reconstruction of the accept rule, not the rule itself — {@code
   * resolveColumn} enumerates nothing, it tries {@code FIELD_MAP}, then the bare column, then the
   * underscored form, then throws. So the set could drift from what the compiler actually takes
   * with nothing to notice, and CHESSQL.md (which is pinned to the set, and served to MCP clients
   * as {@code chessql://reference}) would confidently document a roster the compiler no longer
   * honours. This is the round trip that ties the two together.
   *
   * <p>One direction only, deliberately: advertised ⊆ accepted. The converse cannot be asserted
   * without enumerating every string the compiler might take, which {@code resolveColumn} does not
   * expose. Widening through the maps is still covered, just not here — a column added to {@code
   * VALID_COLUMNS} lands in {@code filterableFields()} too, so {@code ChessQlReferenceTest} and
   * {@code McpToolVocabularyTest} both fail until the doc and the tool description catch up
   * (verified by mutation). What nothing catches is a new special case inside {@code resolveColumn}
   * itself.
   */
  @Test
  public void everyFilterableFieldActuallyCompiles() {
    for (String field : SqlCompiler.filterableFields()) {
      String literal =
          switch (field) {
            case "date" -> "\"2026-07-01\"";
            case "month" -> "\"2026-07\"";
            case "played.at" -> "\"2026-07-01T13:30:00\"";
            case "white.elo", "black.elo", "num.moves" -> "2500";
            default -> "\"x\"";
          };
      assertThatCode(() -> compile(field + " = " + literal))
          .as("%s is advertised by filterableFields() but the compiler rejects it", field)
          .doesNotThrowAnyException();
    }
  }

  /**
   * The negative twin. Without it the test above passes against a {@code filterableFields()} that
   * returned everything, or a compiler that accepted everything — and "accepts everything" is the
   * failure mode that turns an unknown field into a silent empty result rather than an error.
   */
  @Test
  public void aFieldOutsideTheAdvertisedSetIsRejected() {
    assertThat(SqlCompiler.filterableFields()).doesNotContain("white.rating");
    assertThatThrownBy(() -> compile("white.rating = 2500"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unknown field");
  }

  /** Likewise for motifs: everything advertised compiles, and nothing outside the set does. */
  @Test
  public void everyAdvertisedMotifCompilesAndNothingElseDoes() {
    for (String motif : SqlCompiler.motifs()) {
      assertThatCode(() -> compile("motif(" + motif + ")"))
          .as("%s is advertised by motifs() but the compiler rejects it", motif)
          .doesNotThrowAnyException();
    }
    assertThat(SqlCompiler.motifs()).doesNotContain("windmill");
    assertThatThrownBy(() -> compile("motif(windmill)"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unknown motif");
  }

  /**
   * Perspective fields need a player, so they compile through the two-argument overload. Advertised
   * means usable: without this, {@code perspectiveFields()} could name a field that only the
   * groupBy path accepts.
   */
  @Test
  public void everyPerspectiveFieldCompilesWithAPlayer() {
    for (String field : SqlCompiler.perspectiveFields()) {
      String literal = field.endsWith(".elo") ? "1500" : "\"x\"";
      assertThatCode(() -> compiler.compile(Parser.parse(field + " = " + literal), "hikaru"))
          .as("%s is advertised by perspectiveFields() but the compiler rejects it", field)
          .doesNotThrowAnyException();
    }
  }

  private CompiledQuery compile(String input) {
    return compiler.compile(Parser.parse(input));
  }
}
