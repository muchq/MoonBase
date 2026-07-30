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
import java.sql.Timestamp;
import java.time.Instant;
import java.time.LocalDate;
import java.time.YearMonth;
import java.time.ZoneOffset;
import java.time.format.DateTimeParseException;
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

  /**
   * Virtual date-scoping fields compiled against the played_at TIMESTAMP column. Values are
   * validated ISO strings ({@code date = "YYYY-MM-DD"}, {@code month = "YYYY-MM"}); every operator
   * is rewritten to plain played_at comparisons against UTC day/month boundaries bound as
   * timestamps, which behaves identically on H2 and Postgres (no dialect date functions).
   */
  private static final String DATE_FIELD = "date";

  private static final String MONTH_FIELD = "month";

  /**
   * Perspective fields resolve the white/black columns relative to a player supplied at compile
   * time, so "hikaru's results regardless of color" is one predicate instead of a hand-written
   * union. Whenever a query uses one, the compiled WHERE clause is additionally guarded by a
   * participation predicate (the player is one of the two sides), because the CASE expressions
   * treat "not white" as "black".
   */
  private record PerspectiveField(String sql, int playerParams) {}

  private static final String ME_IS_WHITE = "LOWER(white_username) = LOWER(?)";

  private static final Map<String, PerspectiveField> PERSPECTIVE_FIELDS =
      Map.of(
          "me.color",
          new PerspectiveField("CASE WHEN " + ME_IS_WHITE + " THEN 'white' ELSE 'black' END", 1),
          "me.elo",
          new PerspectiveField(
              "CASE WHEN " + ME_IS_WHITE + " THEN white_elo ELSE black_elo END", 1),
          "me.title",
          new PerspectiveField(
              "CASE WHEN " + ME_IS_WHITE + " THEN white_title ELSE black_title END", 1),
          "opponent.username",
          new PerspectiveField(
              "CASE WHEN " + ME_IS_WHITE + " THEN black_username ELSE white_username END", 1),
          "opponent.elo",
          new PerspectiveField(
              "CASE WHEN " + ME_IS_WHITE + " THEN black_elo ELSE white_elo END", 1),
          "opponent.title",
          new PerspectiveField(
              "CASE WHEN " + ME_IS_WHITE + " THEN black_title ELSE white_title END", 1),
          "outcome",
          new PerspectiveField(
              "CASE WHEN result = '1/2-1/2' THEN 'draw'"
                  + " WHEN (result = '1-0' AND LOWER(white_username) = LOWER(?))"
                  + " OR (result = '0-1' AND LOWER(black_username) = LOWER(?)) THEN 'win'"
                  + " WHEN result IN ('1-0', '0-1') THEN 'loss' ELSE 'unknown' END",
              2));

  private static final Set<String> STRING_PERSPECTIVE_FIELDS =
      Set.of("me.color", "me.title", "opponent.username", "opponent.title", "outcome");

  /** Mutable compile-scope state: the player (if any) and whether a perspective field was used. */
  private static final class Perspective {
    private final String player;
    private boolean used;

    private Perspective(String player) {
      this.player = player == null || player.isBlank() ? null : player.strip();
    }
  }

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
    return compile(pq, null);
  }

  /**
   * Compiles with an optional perspective player, which perspective fields (me.*, opponent.*,
   * outcome) are resolved against. Queries that use perspective fields require a non-blank player.
   */
  public CompiledQuery compile(ParsedQuery pq, String player) {
    Perspective perspective = new Perspective(player);
    List<Object> whereParams = new ArrayList<>();
    String whereClause = compileExpr(pq.expr(), whereParams, perspective);
    whereClause = guardParticipation(whereClause, whereParams, perspective);

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
    return compileAggregate(pq, groupByFields, null);
  }

  /**
   * Aggregate variant of {@link #compile(ParsedQuery, String)}: the filter may use perspective
   * fields (resolved against {@code player}); group-by fields must be physical columns, except for
   * {@code me.color} and {@code outcome}, which are groupable when a player is supplied.
   */
  public CompiledQuery compileAggregate(ParsedQuery pq, List<String> groupByFields, String player) {
    if (pq.orderBy() != null) {
      throw new IllegalArgumentException(
          "ORDER BY motif_count is not supported in aggregate queries");
    }
    List<GroupByTerm> terms = resolveGroupByTerms(groupByFields);
    Perspective perspective = new Perspective(player);

    // SELECT-list group expressions render (and bind their player params) before the WHERE clause.
    List<Object> selectParams = new ArrayList<>();
    List<String> selectExprs = new ArrayList<>();
    for (GroupByTerm term : terms) {
      if (term.perspectiveField() == null) {
        selectExprs.add(term.key());
      } else {
        String expr =
            perspectiveExpr(
                term.perspectiveField(),
                PERSPECTIVE_FIELDS.get(term.perspectiveField()),
                perspective,
                selectParams);
        selectExprs.add(expr + " AS " + term.key());
      }
    }

    List<Object> whereParams = new ArrayList<>();
    String whereClause = compileExpr(pq.expr(), whereParams, perspective);
    whereClause = guardParticipation(whereClause, whereParams, perspective);

    // GROUP BY references the term keys: the column itself for physical fields, the SELECT alias
    // for perspective fields. Repeating the CASE expression instead would not be portable —
    // Postgres matches SELECT expressions to GROUP BY expressions structurally, and a second
    // rendering carries different bind-placeholder positions.
    String keys = terms.stream().map(GroupByTerm::key).collect(Collectors.joining(", "));
    String tiebreak = terms.stream().map(t -> t.key() + " ASC").collect(Collectors.joining(", "));
    String sql =
        "SELECT "
            + String.join(", ", selectExprs)
            + ", COUNT(*) AS group_count FROM game_features g WHERE "
            + whereClause
            + " GROUP BY "
            + keys
            + " ORDER BY group_count DESC, "
            + tiebreak;
    List<Object> params = new ArrayList<>(selectParams);
    params.addAll(whereParams);
    return new CompiledQuery(sql, params);
  }

  /** Totals variant of {@link #compileAggregateTotals(ParsedQuery, List, String)} sans player. */
  public CompiledQuery compileAggregateTotals(ParsedQuery pq, List<String> groupByFields) {
    return compileAggregateTotals(pq, groupByFields, null);
  }

  /**
   * Companion to {@link #compileAggregate(ParsedQuery, List, String)}: compiles the same filter and
   * grouping into a single-row totals query ({@code total_groups}, {@code total_games}) over the
   * untruncated result, so callers applying a group limit can report how much was cut off.
   */
  public CompiledQuery compileAggregateTotals(
      ParsedQuery pq, List<String> groupByFields, String player) {
    if (pq.orderBy() != null) {
      throw new IllegalArgumentException(
          "ORDER BY motif_count is not supported in aggregate queries");
    }
    List<GroupByTerm> terms = resolveGroupByTerms(groupByFields);
    Perspective perspective = new Perspective(player);

    List<Object> whereParams = new ArrayList<>();
    String whereClause = compileExpr(pq.expr(), whereParams, perspective);

    // The inner query has no SELECT-list group expressions to match, so perspective terms can
    // inline their CASE expression directly in GROUP BY (params bind after the WHERE clause).
    List<Object> groupParams = new ArrayList<>();
    List<String> groupExprs = new ArrayList<>();
    for (GroupByTerm term : terms) {
      if (term.perspectiveField() == null) {
        groupExprs.add(term.key());
      } else {
        groupExprs.add(
            perspectiveExpr(
                term.perspectiveField(),
                PERSPECTIVE_FIELDS.get(term.perspectiveField()),
                perspective,
                groupParams));
      }
    }
    whereClause = guardParticipation(whereClause, whereParams, perspective);

    String sql =
        "SELECT COUNT(*) AS total_groups, COALESCE(SUM(group_count), 0) AS total_games FROM ("
            + "SELECT COUNT(*) AS group_count FROM game_features g WHERE "
            + whereClause
            + " GROUP BY "
            + String.join(", ", groupExprs)
            + ") grp";
    List<Object> params = new ArrayList<>(whereParams);
    params.addAll(groupParams);
    return new CompiledQuery(sql, params);
  }

  /**
   * Resolves group-by fields (dotted or underscore form) to their canonical group keys,
   * deduplicating while preserving order. Physical columns resolve to their column name; groupable
   * perspective fields resolve to their underscore form ({@code me_color}, {@code outcome}), which
   * is also the SELECT alias aggregate rows are keyed by. Throws on unknown fields.
   */
  public List<String> resolveGroupByColumns(List<String> groupByFields) {
    return resolveGroupByTerms(groupByFields).stream().map(GroupByTerm::key).toList();
  }

  /**
   * One resolved groupBy term: the group key (canonical column name, or underscore perspective name
   * used as the SELECT alias) and, for perspective terms, the dotted field to render the CASE
   * expression from ({@code null} for physical columns).
   */
  private record GroupByTerm(String key, String perspectiveField) {}

  /** Perspective fields allowed in groupBy, keyed by every accepted spelling. */
  private static final Map<String, String> GROUPABLE_PERSPECTIVE_FIELDS =
      Map.of("me.color", "me.color", "me_color", "me.color", "outcome", "outcome");

  private List<GroupByTerm> resolveGroupByTerms(List<String> groupByFields) {
    if (groupByFields == null || groupByFields.isEmpty()) {
      throw new IllegalArgumentException("groupBy requires at least one field");
    }
    List<GroupByTerm> terms = new ArrayList<>();
    for (String field : groupByFields) {
      GroupByTerm term = resolveGroupByTerm(field);
      if (terms.stream().noneMatch(t -> t.key().equals(term.key()))) {
        terms.add(term);
      }
    }
    return terms;
  }

  private GroupByTerm resolveGroupByTerm(String field) {
    String perspectiveField = GROUPABLE_PERSPECTIVE_FIELDS.get(field);
    if (perspectiveField != null) {
      return new GroupByTerm(perspectiveField.replace('.', '_'), perspectiveField);
    }
    if (PERSPECTIVE_FIELDS.containsKey(field)) {
      throw new IllegalArgumentException(
          "Perspective fields are not supported in groupBy: "
              + field
              + " (only me.color and outcome are groupable, with a player)");
    }
    if (DATE_FIELD.equals(field) || MONTH_FIELD.equals(field)) {
      throw new IllegalArgumentException(
          "'" + field + "' is a filter-only field and is not supported in groupBy");
    }
    return new GroupByTerm(resolveColumn(field), null);
  }

  /**
   * When any perspective field was used, restricts the WHERE clause to games the player actually
   * participated in (prepending the two player bind params so placeholder order matches the SQL).
   */
  private static String guardParticipation(
      String whereClause, List<Object> params, Perspective perspective) {
    if (!perspective.used) {
      return whereClause;
    }
    params.add(0, perspective.player);
    params.add(1, perspective.player);
    return "((LOWER(white_username) = LOWER(?) OR LOWER(black_username) = LOWER(?)) AND "
        + whereClause
        + ")";
  }

  private String compileExpr(Expr expr, List<Object> params, Perspective perspective) {
    return switch (expr) {
      case OrExpr or ->
          or.operands().stream()
              .map(e -> compileExpr(e, params, perspective))
              .collect(Collectors.joining(" OR ", "(", ")"));
      case AndExpr and ->
          and.operands().stream()
              .map(e -> compileExpr(e, params, perspective))
              .collect(Collectors.joining(" AND ", "(", ")"));
      case NotExpr not -> "(NOT " + compileExpr(not.operand(), params, perspective) + ")";
      case ComparisonExpr cmp -> compileComparison(cmp, params, perspective);
      case InExpr in -> compileIn(in, params, perspective);
      case MotifExpr motif -> compileMotif(motif);
      case SequenceExpr seq -> compileSequence(seq, params);
    };
  }

  private String compileComparison(
      ComparisonExpr cmp, List<Object> params, Perspective perspective) {
    String op = cmp.operator();
    if (!VALID_OPS.contains(op)) {
      throw new IllegalArgumentException("Invalid operator: " + op);
    }

    if (DATE_FIELD.equals(cmp.field()) || MONTH_FIELD.equals(cmp.field())) {
      return compileDateComparison(cmp, params);
    }

    PerspectiveField perspectiveField = PERSPECTIVE_FIELDS.get(cmp.field());
    if (perspectiveField != null) {
      String expr = perspectiveExpr(cmp.field(), perspectiveField, perspective, params);
      params.add(cmp.value());
      if (STRING_PERSPECTIVE_FIELDS.contains(cmp.field()) && (op.equals("=") || op.equals("!="))) {
        return "LOWER" + expr + " " + op + " LOWER(?)";
      }
      return expr + " " + op + " ?";
    }

    String column = resolveColumn(cmp.field());
    params.add(cmp.value());
    if (STRING_COLUMNS.contains(column) && (op.equals("=") || op.equals("!="))) {
      return "LOWER(" + column + ") " + op + " LOWER(?)";
    }
    return column + " " + op + " ?";
  }

  /**
   * Compiles a {@code date} / {@code month} comparison to played_at range predicates. Day and month
   * values cover a half-open timestamp interval, so operators are rewritten against the interval's
   * boundaries: {@code date = "D"} means "played on day D", {@code date <= "D"} includes all of day
   * D, and so on. Boundaries are UTC and bound as timestamps.
   */
  private static String compileDateComparison(ComparisonExpr cmp, List<Object> params) {
    Instant start;
    Instant end;
    if (MONTH_FIELD.equals(cmp.field())) {
      if (!cmp.operator().equals("=")) {
        throw new IllegalArgumentException(
            "month supports only '=' (use date for range comparisons), got: " + cmp.operator());
      }
      YearMonth month = parseMonthValue(cmp.value());
      start = month.atDay(1).atStartOfDay(ZoneOffset.UTC).toInstant();
      end = month.plusMonths(1).atDay(1).atStartOfDay(ZoneOffset.UTC).toInstant();
    } else {
      LocalDate day = parseDateValue(cmp.value());
      start = day.atStartOfDay(ZoneOffset.UTC).toInstant();
      end = day.plusDays(1).atStartOfDay(ZoneOffset.UTC).toInstant();
    }
    switch (cmp.operator()) {
      case "=":
        params.add(Timestamp.from(start));
        params.add(Timestamp.from(end));
        return "(played_at >= ? AND played_at < ?)";
      case "!=":
        params.add(Timestamp.from(start));
        params.add(Timestamp.from(end));
        return "(played_at < ? OR played_at >= ?)";
      case "<":
        params.add(Timestamp.from(start));
        return "played_at < ?";
      case ">=":
        params.add(Timestamp.from(start));
        return "played_at >= ?";
      case "<=":
        params.add(Timestamp.from(end));
        return "played_at < ?";
      case ">":
        params.add(Timestamp.from(end));
        return "played_at >= ?";
      default:
        throw new IllegalArgumentException("Invalid operator: " + cmp.operator());
    }
  }

  private static LocalDate parseDateValue(Object value) {
    if (value instanceof String s) {
      try {
        return LocalDate.parse(s);
      } catch (DateTimeParseException e) {
        // fall through to the shared error below
      }
    }
    throw new IllegalArgumentException(
        "date requires an ISO date string (\"YYYY-MM-DD\"), got: " + value);
  }

  private static YearMonth parseMonthValue(Object value) {
    if (value instanceof String s) {
      try {
        return YearMonth.parse(s);
      } catch (DateTimeParseException e) {
        // fall through to the shared error below
      }
    }
    throw new IllegalArgumentException("month requires a \"YYYY-MM\" string, got: " + value);
  }

  private String compileIn(InExpr in, List<Object> params, Perspective perspective) {
    if (DATE_FIELD.equals(in.field()) || MONTH_FIELD.equals(in.field())) {
      throw new IllegalArgumentException(
          in.field() + " does not support IN; use comparisons (e.g. date >= \"2026-07-01\")");
    }
    PerspectiveField perspectiveField = PERSPECTIVE_FIELDS.get(in.field());
    if (perspectiveField != null) {
      String expr = perspectiveExpr(in.field(), perspectiveField, perspective, params);
      params.addAll(in.values());
      if (STRING_PERSPECTIVE_FIELDS.contains(in.field())) {
        String lowerPlaceholders =
            in.values().stream().map(v -> "LOWER(?)").collect(Collectors.joining(", "));
        return "LOWER" + expr + " IN (" + lowerPlaceholders + ")";
      }
      String placeholders = in.values().stream().map(v -> "?").collect(Collectors.joining(", "));
      return expr + " IN (" + placeholders + ")";
    }

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

  /** Renders a perspective field's CASE expression, binding its player param(s) in SQL order. */
  private static String perspectiveExpr(
      String field,
      PerspectiveField perspectiveField,
      Perspective perspective,
      List<Object> params) {
    if (perspective.player == null) {
      throw new IllegalArgumentException(
          "Field '"
              + field
              + "' is perspective-relative (me.*, opponent.*, outcome) and requires a player"
              + " parameter on the request");
    }
    perspective.used = true;
    for (int i = 0; i < perspectiveField.playerParams(); i++) {
      params.add(perspective.player);
    }
    return "(" + perspectiveField.sql() + ")";
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
