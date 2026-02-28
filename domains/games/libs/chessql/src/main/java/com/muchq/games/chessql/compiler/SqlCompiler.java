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
          "sacrifice",
          "zugzwang",
          "double_check",
          "interference",
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
    List<Object> whereParams = new ArrayList<>();
    String whereClause = compileExpr(pq.expr(), whereParams);

    OrderByClause orderBy = pq.orderBy();
    if (orderBy != null) {
      String motifName = orderBy.motifName();
      if (!VALID_MOTIFS.contains(motifName)) {
        throw new IllegalArgumentException("Unknown motif in ORDER BY: " + motifName);
      }
      String motifDbValue = motifName.toUpperCase();
      String direction = orderBy.ascending() ? "ASC" : "DESC";

      // The LEFT JOIN subquery param must come before the WHERE params.
      List<Object> allParams = new ArrayList<>();
      allParams.add(motifDbValue);
      allParams.addAll(whereParams);

      String sql =
          "SELECT g.* FROM game_features g"
              + " LEFT JOIN (SELECT game_url, COUNT(*) AS c FROM motif_occurrences"
              + " WHERE motif = ? GROUP BY game_url) cnt"
              + " ON g.game_url = cnt.game_url"
              + " WHERE "
              + whereClause
              + " ORDER BY COALESCE(cnt.c, 0) "
              + direction;
      return new CompiledQuery(sql, allParams);
    } else {
      String sql =
          "SELECT g.* FROM game_features g WHERE " + whereClause + " ORDER BY g.played_at DESC";
      return new CompiledQuery(sql, whereParams);
    }
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
    // Several motifs are derived from ATTACK occurrences rather than stored as their own rows.
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

    // Build: EXISTS (SELECT 1 FROM motif_occurrences mo1
    //   JOIN motif_occurrences mo2 ON mo2.game_url = mo1.game_url AND mo2.motif = ? AND mo2.ply =
    // mo1.ply + 2
    //   [ ... more JOINs for longer sequences ... ]
    //   WHERE mo1.game_url = g.game_url AND mo1.motif = ?)
    // Parameters: names[1].toUpperCase(), names[2].toUpperCase(), ..., names[0].toUpperCase()
    StringBuilder sb = new StringBuilder("EXISTS (SELECT 1 FROM motif_occurrences mo1");

    for (int i = 1; i < names.size(); i++) {
      int prev = i;
      int cur = i + 1;
      sb.append(
          String.format(
              " JOIN motif_occurrences mo%d ON mo%d.game_url = mo1.game_url"
                  + " AND mo%d.motif = ? AND mo%d.ply = mo%d.ply + 2",
              cur, cur, cur, cur, prev));
      params.add(names.get(i).toUpperCase());
    }

    sb.append(" WHERE mo1.game_url = g.game_url AND mo1.motif = ?)");
    params.add(names.get(0).toUpperCase());

    return sb.toString();
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
