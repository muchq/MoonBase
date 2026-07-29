package com.muchq.games.chessql.compiler;

import com.muchq.games.chessql.ast.AndExpr;
import com.muchq.games.chessql.ast.ComparisonExpr;
import com.muchq.games.chessql.ast.Expr;
import com.muchq.games.chessql.ast.InExpr;
import com.muchq.games.chessql.ast.MotifExpr;
import com.muchq.games.chessql.ast.NotExpr;
import com.muchq.games.chessql.ast.OrExpr;
import com.muchq.games.chessql.ast.OrderByClause;
import com.muchq.games.chessql.ast.SequenceExpr;
import com.muchq.games.chessql.parser.ParsedQuery;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;

public class SqlCompiler implements QueryCompiler<CompiledQuery> {

  /**
   * Motifs whose WHERE-clause detection is derived from ATTACK rows at query time. These motifs
   * have no stored rows of their own in motif_occurrences.
   *
   * <p>Note: discovered_attack, checkmate, discovered_check, and double_check are ALSO expressed
   * via ATTACK rows in compileMotif(), but they DO have stored rows from their dedicated detectors.
   * ORDER BY and sequence() for those motifs use stored rows and work normally. See GitHub issue
   * #1083 for the consistency follow-up on those motifs.
   */
  private static final Set<String> ATTACK_DERIVED_MOTIFS = Set.of("fork");

  private static final Set<String> VALID_COLUMNS =
      Set.of(
          "white_username",
          "black_username",
          "white_elo",
          "black_elo",
          "white_title",
          "black_title",
          "time_class",
          "eco",
          "opening_name",
          "opening_family",
          "result",
          "num_moves",
          "platform",
          "game_url",
          "played_at");

  private static final Set<String> VALID_MOTIFS =
      Set.of(
          "pin",
          "cross_pin",
          "fork",
          "skewer",
          "discovered_attack",
          "discovered_check",
          "check",
          "checkmate",
          "promotion",
          "promotion_with_check",
          "promotion_with_checkmate",
          "back_rank_mate",
          "smothered_mate",
          "zugzwang",
          "double_check",
          "overloaded_piece");

  private static final Map<String, String> FIELD_MAP =
      Map.ofEntries(
          Map.entry("white.elo", "white_elo"),
          Map.entry("black.elo", "black_elo"),
          Map.entry("white.username", "white_username"),
          Map.entry("black.username", "black_username"),
          Map.entry("white.title", "white_title"),
          Map.entry("black.title", "black_title"),
          Map.entry("time.class", "time_class"),
          Map.entry("num.moves", "num_moves"),
          Map.entry("game.url", "game_url"),
          Map.entry("played.at", "played_at"),
          Map.entry("opening.name", "opening_name"),
          Map.entry("opening.family", "opening_family"));

  private static final Set<String> VALID_OPS = Set.of("=", "!=", "<", "<=", ">", ">=");

  private static final Set<String> STRING_COLUMNS =
      Set.of(
          "white_username",
          "black_username",
          "white_title",
          "black_title",
          "time_class",
          "eco",
          "opening_name",
          "opening_family",
          "result",
          "platform",
          "game_url");

  @Override
  public CompiledQuery compile(ParsedQuery pq) {
    List<Object> whereParams = new ArrayList<>();
    String whereClause = compileExpr(pq.expr(), whereParams);

    OrderByClause orderBy = pq.orderBy();
    if (orderBy != null) {
      String motifName = orderBy.motifName();
      if (!VALID_MOTIFS.contains(motifName)) {
        throw new IllegalArgumentException("Unknown motif in ORDER BY: " + motifName);
      }
      String direction = orderBy.ascending() ? "ASC" : "DESC";

      String countSubquery;
      List<Object> allParams = new ArrayList<>();
      if (ATTACK_DERIVED_MOTIFS.contains(motifName)) {
        // Derived motifs: count distinct occurrences via ATTACK rows (no extra param needed)
        countSubquery = forkCountSubquery();
        allParams.addAll(whereParams);
      } else {
        // Stored motifs: count rows directly; param must come before WHERE params
        allParams.add(motifName.toUpperCase());
        allParams.addAll(whereParams);
        countSubquery =
            "SELECT game_url, COUNT(*) AS c FROM motif_occurrences WHERE motif = ? GROUP BY"
                + " game_url";
      }

      String sql =
          "SELECT g.* FROM game_features g"
              + " LEFT JOIN ("
              + countSubquery
              + ") cnt"
              + " ON g.game_url = cnt.game_url"
              + " WHERE "
              + whereClause
              + " ORDER BY COALESCE(cnt.c, 0) "
              + direction
              + ", g.game_url ASC";
      return new CompiledQuery(sql, allParams);
    } else {
      String sql =
          "SELECT g.* FROM game_features g WHERE "
              + whereClause
              + " ORDER BY g.played_at DESC, g.game_url ASC";
      return new CompiledQuery(sql, whereParams);
    }
  }

  /**
   * Compiles a ChessQL filter into a grouped count query: {@code SELECT <cols>, COUNT(*) AS
   * group_count ... GROUP BY <cols> ORDER BY group_count DESC}. Group columns are validated against
   * the same column whitelist as comparisons, so this adds no injection surface; the caller appends
   * LIMIT via a bind parameter.
   */
  public CompiledQuery compileAggregate(ParsedQuery pq, List<String> groupByFields) {
    if (pq.orderBy() != null) {
      throw new IllegalArgumentException(
          "ORDER BY motif_count is not supported in aggregate queries");
    }
    List<String> columns = resolveGroupByColumns(groupByFields);

    List<Object> params = new ArrayList<>();
    String whereClause = compileExpr(pq.expr(), params);

    String cols = String.join(", ", columns);
    String tiebreak = columns.stream().map(c -> c + " ASC").collect(Collectors.joining(", "));
    String sql =
        "SELECT "
            + cols
            + ", COUNT(*) AS group_count FROM game_features g WHERE "
            + whereClause
            + " GROUP BY "
            + cols
            + " ORDER BY group_count DESC, "
            + tiebreak;
    return new CompiledQuery(sql, params);
  }

  /**
   * Resolves group-by fields (dotted or underscore form) to their canonical column names,
   * deduplicating while preserving order. Throws on unknown fields.
   */
  public List<String> resolveGroupByColumns(List<String> groupByFields) {
    if (groupByFields == null || groupByFields.isEmpty()) {
      throw new IllegalArgumentException("groupBy requires at least one field");
    }
    List<String> columns = new ArrayList<>();
    for (String field : groupByFields) {
      String column = resolveColumn(field);
      if (!columns.contains(column)) {
        columns.add(column);
      }
    }
    return columns;
  }

  private String compileExpr(Expr expr, List<Object> params) {
    return switch (expr) {
      case OrExpr or ->
          or.operands().stream()
              .map(e -> compileExpr(e, params))
              .collect(Collectors.joining(" OR ", "(", ")"));
      case AndExpr and ->
          and.operands().stream()
              .map(e -> compileExpr(e, params))
              .collect(Collectors.joining(" AND ", "(", ")"));
      case NotExpr not -> "(NOT " + compileExpr(not.operand(), params) + ")";
      case ComparisonExpr cmp -> compileComparison(cmp, params);
      case InExpr in -> compileIn(in, params);
      case MotifExpr motif -> compileMotif(motif);
      case SequenceExpr seq -> compileSequence(seq, params);
    };
  }

  private String compileComparison(ComparisonExpr cmp, List<Object> params) {
    String column = resolveColumn(cmp.field());
    String op = cmp.operator();
    if (!VALID_OPS.contains(op)) {
      throw new IllegalArgumentException("Invalid operator: " + op);
    }
    params.add(cmp.value());
    if (STRING_COLUMNS.contains(column) && (op.equals("=") || op.equals("!="))) {
      return "LOWER(" + column + ") " + op + " LOWER(?)";
    }
    return column + " " + op + " ?";
  }

  private String compileIn(InExpr in, List<Object> params) {
    String column = resolveColumn(in.field());
    params.addAll(in.values());
    if (STRING_COLUMNS.contains(column)) {
      String lowerPlaceholders =
          in.values().stream().map(v -> "LOWER(?)").collect(Collectors.joining(", "));
      return "LOWER(" + column + ") IN (" + lowerPlaceholders + ")";
    }
    String placeholders = in.values().stream().map(v -> "?").collect(Collectors.joining(", "));
    return column + " IN (" + placeholders + ")";
  }

  private String compileMotif(MotifExpr motif) {
    String name = motif.motifName();
    if (!VALID_MOTIFS.contains(name)) {
      throw new IllegalArgumentException("Unknown motif: " + name);
    }
    return switch (name) {
      // Derived: ATTACK where the revealing piece uncovers the attack (is_discovered flag)
      case "discovered_attack" ->
          "EXISTS (SELECT 1 FROM motif_occurrences mo"
              + " WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK'"
              + " AND mo.is_discovered = TRUE)";
      // Derived: ATTACK that delivers checkmate (is_mate flag)
      case "checkmate" ->
          "EXISTS (SELECT 1 FROM motif_occurrences mo"
              + " WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK'"
              + " AND mo.is_mate = TRUE)";
      // Derived: discovered ATTACK whose target is the king
      case "discovered_check" ->
          "EXISTS (SELECT 1 FROM motif_occurrences mo"
              + " WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK'"
              + " AND mo.is_discovered = TRUE"
              + " AND (mo.target LIKE 'K%' OR mo.target LIKE 'k%'))";
      // Derived: same attacker at same ply hits 2+ targets (non-discovered, attacker non-null)
      case "fork" ->
          "EXISTS (SELECT 1 FROM motif_occurrences mo"
              + " WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK'"
              + " AND mo.is_discovered = FALSE AND mo.attacker IS NOT NULL"
              + " GROUP BY mo.ply, mo.attacker HAVING COUNT(*) >= 2)";
      // Derived: 2+ ATTACK rows at the same ply each targeting the king
      case "double_check" ->
          "EXISTS (SELECT 1 FROM motif_occurrences mo"
              + " WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK'"
              + " AND (mo.target LIKE 'K%' OR mo.target LIKE 'k%')"
              + " GROUP BY mo.ply HAVING COUNT(*) >= 2)";
      // All other motifs are stored directly in motif_occurrences under their own name
      default -> {
        String motifDbValue = name.toUpperCase();
        yield "EXISTS (SELECT 1 FROM motif_occurrences mo"
            + " WHERE mo.game_url = g.game_url AND mo.motif = '"
            + motifDbValue
            + "')";
      }
    };
  }

  /**
   * Compiles a sequence expression into a correlated EXISTS subquery. Every motif — stored or
   * derived — is expressed as a {@code (game_url, ply)} subquery fragment via {@link
   * #motifToPlySubquery}. The fragments are joined on consecutive plies (ply + 2 per step),
   * anchored to the outer game via {@code sq1.game_url = g.game_url}.
   *
   * <p>No bind parameters are added: motif values are validated against {@link #VALID_MOTIFS} and
   * inlined as SQL literals, which is safe against injection.
   */
  private String compileSequence(SequenceExpr seq, List<Object> params) {
    List<String> names = seq.motifNames();
    if (names.size() < 2) {
      throw new IllegalArgumentException("sequence() requires at least 2 motifs");
    }
    for (String name : names) {
      if (!VALID_MOTIFS.contains(name)) {
        throw new IllegalArgumentException("Unknown motif in sequence: " + name);
      }
    }

    StringBuilder sb = new StringBuilder("EXISTS (SELECT 1");
    sb.append(" FROM (").append(motifToPlySubquery(names.get(0))).append(") sq1");

    for (int i = 1; i < names.size(); i++) {
      int sqNum = i + 1;
      int prevSqNum = i;
      sb.append(" JOIN (")
          .append(motifToPlySubquery(names.get(i)))
          .append(") sq")
          .append(sqNum)
          .append(" ON sq")
          .append(sqNum)
          .append(".game_url = sq1.game_url AND sq")
          .append(sqNum)
          .append(".ply = sq")
          .append(prevSqNum)
          .append(".ply + 2");
    }

    sb.append(" WHERE sq1.game_url = g.game_url)");
    return sb.toString();
  }

  /**
   * Returns a {@code SELECT game_url, ply FROM ...} SQL fragment for the given motif. The result
   * has uniform shape regardless of whether the motif is stored directly or derived from ATTACK
   * rows. Used by {@link #compileSequence} and conceptually equivalent to {@link #compileMotif} but
   * returning occurrence positions rather than an existence predicate.
   *
   * <p>All motif name values are inlined as SQL literals; they are safe to inline because they are
   * validated against {@link #VALID_MOTIFS} before this method is called.
   */
  private String motifToPlySubquery(String name) {
    return switch (name) {
      case "fork" ->
          "SELECT game_url, ply FROM motif_occurrences"
              + " WHERE motif = 'ATTACK' AND is_discovered = FALSE AND attacker IS NOT NULL"
              + " GROUP BY game_url, ply, attacker HAVING COUNT(*) >= 2";
      case "discovered_attack" ->
          "SELECT game_url, ply FROM motif_occurrences"
              + " WHERE motif = 'ATTACK' AND is_discovered = TRUE";
      case "checkmate" ->
          "SELECT game_url, ply FROM motif_occurrences"
              + " WHERE motif = 'ATTACK' AND is_mate = TRUE";
      case "discovered_check" ->
          "SELECT game_url, ply FROM motif_occurrences"
              + " WHERE motif = 'ATTACK' AND is_discovered = TRUE"
              + " AND (target LIKE 'K%' OR target LIKE 'k%')";
      case "double_check" ->
          "SELECT game_url, ply FROM motif_occurrences"
              + " WHERE motif = 'ATTACK' AND (target LIKE 'K%' OR target LIKE 'k%')"
              + " GROUP BY game_url, ply HAVING COUNT(*) >= 2";
      default -> {
        // Stored motif: inline the validated name as an uppercase literal.
        String dbValue = name.toUpperCase();
        yield "SELECT game_url, ply FROM motif_occurrences WHERE motif = '" + dbValue + "'";
      }
    };
  }

  /**
   * Returns a subquery that counts the number of distinct fork instances per game. A fork instance
   * is a unique (ply, attacker) pair with 2+ non-discovered ATTACK targets at that ply.
   */
  private static String forkCountSubquery() {
    return "SELECT game_url, COUNT(*) AS c FROM ("
        + "SELECT game_url FROM motif_occurrences"
        + " WHERE motif = 'ATTACK' AND is_discovered = FALSE AND attacker IS NOT NULL"
        + " GROUP BY game_url, ply, attacker HAVING COUNT(*) >= 2"
        + ") forks GROUP BY game_url";
  }

  private String resolveColumn(String field) {
    String mapped = FIELD_MAP.get(field);
    if (mapped != null) {
      return mapped;
    }
    // Try direct column name (already underscore-separated)
    if (VALID_COLUMNS.contains(field)) {
      return field;
    }
    // Try converting dots to underscores
    String underscored = field.replace('.', '_');
    if (VALID_COLUMNS.contains(underscored)) {
      return underscored;
    }
    throw new IllegalArgumentException("Unknown field: " + field);
  }
}
