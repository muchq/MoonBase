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
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;

/**
 * Compiles a ChessQL {@link ParsedQuery} into DataFusion SQL with all values inlined as SQL
 * literals. Unlike {@link SqlCompiler}, no JDBC bind parameters are emitted; the returned {@link
 * CompiledQuery#parameters()} list is always empty.
 */
public class DataFusionSqlCompiler implements QueryCompiler<CompiledQuery> {

  private static final Set<String> ATTACK_DERIVED_MOTIFS = Set.of("fork");

  private static final Set<String> VALID_COLUMNS =
      Set.of(
          "white_username",
          "black_username",
          "white_elo",
          "black_elo",
          "time_class",
          "eco",
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
      Map.of(
          "white.elo", "white_elo",
          "black.elo", "black_elo",
          "white.username", "white_username",
          "black.username", "black_username",
          "time.class", "time_class",
          "num.moves", "num_moves",
          "game.url", "game_url",
          "played.at", "played_at");

  private static final Set<String> VALID_OPS = Set.of("=", "!=", "<", "<=", ">", ">=");

  private static final Set<String> STRING_COLUMNS =
      Set.of(
          "white_username",
          "black_username",
          "time_class",
          "eco",
          "result",
          "platform",
          "game_url");

  @Override
  public CompiledQuery compile(ParsedQuery pq) {
    String whereClause = compileExpr(pq.expr());

    OrderByClause orderBy = pq.orderBy();
    if (orderBy != null) {
      String motifName = orderBy.motifName();
      if (!VALID_MOTIFS.contains(motifName)) {
        throw new IllegalArgumentException("Unknown motif in ORDER BY: " + motifName);
      }
      String direction = orderBy.ascending() ? "ASC" : "DESC";

      String countSubquery;
      if (ATTACK_DERIVED_MOTIFS.contains(motifName)) {
        countSubquery = forkCountSubquery();
      } else {
        countSubquery =
            "SELECT game_url, COUNT(*) AS c FROM motif_occurrences WHERE motif = '"
                + motifName.toUpperCase()
                + "' GROUP BY game_url";
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
              + direction;
      return new CompiledQuery(sql, List.of());
    } else {
      String sql =
          "SELECT g.* FROM game_features g WHERE " + whereClause + " ORDER BY g.played_at DESC";
      return new CompiledQuery(sql, List.of());
    }
  }

  private String compileExpr(Expr expr) {
    return switch (expr) {
      case OrExpr or ->
          or.operands().stream()
              .map(this::compileExpr)
              .collect(Collectors.joining(" OR ", "(", ")"));
      case AndExpr and ->
          and.operands().stream()
              .map(this::compileExpr)
              .collect(Collectors.joining(" AND ", "(", ")"));
      case NotExpr not -> "(NOT " + compileExpr(not.operand()) + ")";
      case ComparisonExpr cmp -> compileComparison(cmp);
      case InExpr in -> compileIn(in);
      case MotifExpr motif -> compileMotif(motif);
      case SequenceExpr seq -> compileSequence(seq);
    };
  }

  private String compileComparison(ComparisonExpr cmp) {
    String column = resolveColumn(cmp.field());
    String op = cmp.operator();
    if (!VALID_OPS.contains(op)) {
      throw new IllegalArgumentException("Invalid operator: " + op);
    }
    if (STRING_COLUMNS.contains(column) && (op.equals("=") || op.equals("!="))) {
      return "lower(" + column + ") " + op + " lower(" + inlineLiteral(column, cmp.value()) + ")";
    }
    return column + " " + op + " " + inlineLiteral(column, cmp.value());
  }

  private String compileIn(InExpr in) {
    String column = resolveColumn(in.field());
    if (STRING_COLUMNS.contains(column)) {
      String lowerLiterals =
          in.values().stream()
              .map(v -> "lower(" + inlineLiteral(column, v) + ")")
              .collect(Collectors.joining(", "));
      return "lower(" + column + ") IN (" + lowerLiterals + ")";
    }
    String literals =
        in.values().stream().map(v -> inlineLiteral(column, v)).collect(Collectors.joining(", "));
    return column + " IN (" + literals + ")";
  }

  private String inlineLiteral(String column, Object value) {
    if (STRING_COLUMNS.contains(column)) {
      return "'" + value.toString().replace("'", "''") + "'";
    }
    return value.toString();
  }

  private String compileMotif(MotifExpr motif) {
    String name = motif.motifName();
    if (!VALID_MOTIFS.contains(name)) {
      throw new IllegalArgumentException("Unknown motif: " + name);
    }
    return switch (name) {
      case "discovered_attack" ->
          "EXISTS (SELECT 1 FROM motif_occurrences mo"
              + " WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK'"
              + " AND mo.is_discovered = TRUE)";
      case "checkmate" ->
          "EXISTS (SELECT 1 FROM motif_occurrences mo"
              + " WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK'"
              + " AND mo.is_mate = TRUE)";
      case "discovered_check" ->
          "EXISTS (SELECT 1 FROM motif_occurrences mo"
              + " WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK'"
              + " AND mo.is_discovered = TRUE"
              + " AND (mo.target LIKE 'K%' OR mo.target LIKE 'k%'))";
      case "fork" ->
          "EXISTS (SELECT 1 FROM motif_occurrences mo"
              + " WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK'"
              + " AND mo.is_discovered = FALSE AND mo.attacker IS NOT NULL"
              + " GROUP BY mo.ply, mo.attacker HAVING COUNT(*) >= 2)";
      case "double_check" ->
          "EXISTS (SELECT 1 FROM motif_occurrences mo"
              + " WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK'"
              + " AND (mo.target LIKE 'K%' OR mo.target LIKE 'k%')"
              + " GROUP BY mo.ply HAVING COUNT(*) >= 2)";
      default -> {
        String motifDbValue = name.toUpperCase();
        yield "EXISTS (SELECT 1 FROM motif_occurrences mo"
            + " WHERE mo.game_url = g.game_url AND mo.motif = '"
            + motifDbValue
            + "')";
      }
    };
  }

  private String compileSequence(SequenceExpr seq) {
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
        String dbValue = name.toUpperCase();
        yield "SELECT game_url, ply FROM motif_occurrences WHERE motif = '" + dbValue + "'";
      }
    };
  }

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
    if (VALID_COLUMNS.contains(field)) {
      return field;
    }
    String underscored = field.replace('.', '_');
    if (VALID_COLUMNS.contains(underscored)) {
      return underscored;
    }
    throw new IllegalArgumentException("Unknown field: " + field);
  }
}
