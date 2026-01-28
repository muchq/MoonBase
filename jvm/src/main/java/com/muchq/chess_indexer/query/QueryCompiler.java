package com.muchq.chess_indexer.query;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class QueryCompiler {

  private static final Map<String, String> FIELD_MAP = new HashMap<>();

  static {
    FIELD_MAP.put("platform", "g.platform");
    FIELD_MAP.put("game.uuid", "g.game_uuid");
    FIELD_MAP.put("end_time", "g.end_time");
    FIELD_MAP.put("rated", "g.rated");
    FIELD_MAP.put("time.class", "g.time_class");
    FIELD_MAP.put("rules", "g.rules");
    FIELD_MAP.put("eco", "g.eco");
    FIELD_MAP.put("result", "g.result");
    FIELD_MAP.put("white.username", "g.white_username");
    FIELD_MAP.put("white.elo", "g.white_elo");
    FIELD_MAP.put("black.username", "g.black_username");
    FIELD_MAP.put("black.elo", "g.black_elo");
    FIELD_MAP.put("features.total_plies", "f.total_plies");
    FIELD_MAP.put("features.has_castle", "f.has_castle");
    FIELD_MAP.put("features.has_promotion", "f.has_promotion");
    FIELD_MAP.put("features.has_check", "f.has_check");
    FIELD_MAP.put("features.has_checkmate", "f.has_checkmate");
  }

  public CompiledQuery compile(Expr expr, int limit) {
    List<Object> params = new ArrayList<>();
    String where = compileExpr(expr, params);
    String sql = """
        SELECT g.id, g.platform, g.game_uuid, g.end_time, g.white_username, g.white_elo,
               g.black_username, g.black_elo, g.result, g.time_class, g.eco, g.rated
          FROM games g
          LEFT JOIN game_features f ON g.id = f.game_id
         WHERE %s
         ORDER BY g.end_time DESC
         LIMIT ?
        """.formatted(where);
    params.add(limit);
    return new CompiledQuery(sql, params);
  }

  private String compileExpr(Expr expr, List<Object> params) {
    if (expr instanceof AndExpr andExpr) {
      return "(" + compileExpr(andExpr.left(), params) + " AND " + compileExpr(andExpr.right(), params) + ")";
    }
    if (expr instanceof OrExpr orExpr) {
      return "(" + compileExpr(orExpr.left(), params) + " OR " + compileExpr(orExpr.right(), params) + ")";
    }
    if (expr instanceof NotExpr notExpr) {
      return "(NOT " + compileExpr(notExpr.expr(), params) + ")";
    }
    if (expr instanceof CompareExpr compareExpr) {
      return compileCompare(compareExpr, params);
    }
    if (expr instanceof InExpr inExpr) {
      return compileIn(inExpr, params);
    }
    if (expr instanceof FuncCallExpr funcCallExpr) {
      return compileFunc(funcCallExpr, params);
    }
    throw new IllegalArgumentException("Unsupported expression: " + expr.getClass().getSimpleName());
  }

  private String compileCompare(CompareExpr expr, List<Object> params) {
    String field = expr.field().path();
    if ("player".equals(field)) {
      return compilePlayerCompare(expr.op(), expr.value(), params);
    }

    String column = resolveField(expr.field());
    params.add(valueToParam(expr.value()));
    return column + " " + expr.op().sql() + " ?";
  }

  private String compilePlayerCompare(CompareOp op, Value value, List<Object> params) {
    Object param = valueToParam(value);
    if (op == CompareOp.NE) {
      params.add(param);
      params.add(param);
      return "(g.white_username != ? AND g.black_username != ?)";
    }
    params.add(param);
    params.add(param);
    return "(g.white_username " + op.sql() + " ? OR g.black_username " + op.sql() + " ?)";
  }

  private String compileIn(InExpr expr, List<Object> params) {
    String field = expr.field().path();
    if ("player".equals(field)) {
      return compilePlayerIn(expr.values(), params);
    }

    String column = resolveField(expr.field());
    if (expr.values().isEmpty()) {
      return "FALSE";
    }
    StringBuilder sb = new StringBuilder();
    sb.append(column).append(" IN (");
    for (int i = 0; i < expr.values().size(); i++) {
      if (i > 0) {
        sb.append(", ");
      }
      sb.append("?");
      params.add(valueToParam(expr.values().get(i)));
    }
    sb.append(')');
    return sb.toString();
  }

  private String compilePlayerIn(List<Value> values, List<Object> params) {
    if (values.isEmpty()) {
      return "FALSE";
    }
    StringBuilder sb = new StringBuilder();
    sb.append("(g.white_username IN (");
    for (int i = 0; i < values.size(); i++) {
      if (i > 0) {
        sb.append(", ");
      }
      sb.append("?");
      params.add(valueToParam(values.get(i)));
    }
    sb.append(") OR g.black_username IN (");
    for (int i = 0; i < values.size(); i++) {
      if (i > 0) {
        sb.append(", ");
      }
      sb.append("?");
      params.add(valueToParam(values.get(i)));
    }
    sb.append("))");
    return sb.toString();
  }

  private String compileFunc(FuncCallExpr func, List<Object> params) {
    String name = func.name().toLowerCase();
    if ("motif".equals(name)) {
      if (func.args().isEmpty()) {
        throw new IllegalArgumentException("motif() requires a name");
      }
      Object motif = valueToParam(func.args().get(0));
      params.add(motif);
      return "EXISTS (SELECT 1 FROM game_motifs m WHERE m.game_id = g.id AND m.motif_name = ?)";
    }
    throw new IllegalArgumentException("Unknown function: " + func.name());
  }

  private String resolveField(Field field) {
    String column = FIELD_MAP.get(field.path());
    if (column == null) {
      throw new IllegalArgumentException("Unknown field: " + field.path());
    }
    return column;
  }

  private Object valueToParam(Value value) {
    if (value instanceof StringValue stringValue) {
      return stringValue.value();
    }
    if (value instanceof NumberValue numberValue) {
      return numberValue.value();
    }
    if (value instanceof BooleanValue booleanValue) {
      return booleanValue.value();
    }
    if (value instanceof IdentValue identValue) {
      return identValue.value();
    }
    throw new IllegalArgumentException("Unsupported value: " + value.getClass().getSimpleName());
  }
}
