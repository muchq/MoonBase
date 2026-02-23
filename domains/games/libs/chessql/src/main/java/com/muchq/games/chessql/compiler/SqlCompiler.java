package com.muchq.games.chessql.compiler;

import com.muchq.games.chessql.ast.AndExpr;
import com.muchq.games.chessql.ast.ComparisonExpr;
import com.muchq.games.chessql.ast.Expr;
import com.muchq.games.chessql.ast.InExpr;
import com.muchq.games.chessql.ast.MotifExpr;
import com.muchq.games.chessql.ast.NotExpr;
import com.muchq.games.chessql.ast.OrExpr;
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
      Set.of("pin", "cross_pin", "fork", "skewer", "discovered_attack");

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

  public CompiledQuery compile(Expr expr) {
    List<Object> params = new ArrayList<>();
    String sql = compileExpr(expr, params);
    return new CompiledQuery(sql, params);
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
    String placeholders = in.values().stream().map(v -> "?").collect(Collectors.joining(", "));
    if (STRING_COLUMNS.contains(column)) {
      String lowerPlaceholders =
          in.values().stream().map(v -> "LOWER(?)").collect(Collectors.joining(", "));
      return "LOWER(" + column + ") IN (" + lowerPlaceholders + ")";
    }
    return column + " IN (" + placeholders + ")";
  }

  private String compileMotif(MotifExpr motif) {
    String name = motif.motifName();
    if (!VALID_MOTIFS.contains(name)) {
      throw new IllegalArgumentException("Unknown motif: " + name);
    }
    return "has_" + name + " = TRUE";
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
