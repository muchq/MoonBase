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
import java.time.LocalDate;
import java.time.LocalDateTime;
import java.time.YearMonth;
import java.time.format.DateTimeParseException;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
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
   * is rewritten to plain played_at comparisons against day/month boundaries bound as {@link
   * LocalDateTime}, which behaves identically on H2 and Postgres (no dialect date functions).
   */
  private static final String DATE_FIELD = "date";

  private static final String MONTH_FIELD = "month";

  /**
   * How a perspective field groups on /v1/aggregate: CATEGORICAL fields (string-valued) group by
   * value; RATING fields group only as fixed-width buckets — GROUP BY on a raw rating makes one
   * bucket per distinct value, which buries the answer under hundreds of one-game groups. The kind
   * also decides filter-side string semantics (LOWER-wrapped comparisons and IN).
   */
  private enum GroupKind {
    CATEGORICAL,
    RATING
  }

  /**
   * Perspective fields resolve the white/black columns relative to a player supplied at compile
   * time, so "hikaru's results regardless of color" is one predicate instead of a hand-written
   * union. Whenever a query uses one, the compiled WHERE clause is additionally guarded by a
   * participation predicate (the player is one of the two sides), because the CASE expressions
   * treat "not white" as "black".
   */
  private record PerspectiveField(String sql, int playerParams, GroupKind kind) {}

  private static final String ME_IS_WHITE = "LOWER(white_username) = LOWER(?)";

  private static final Map<String, PerspectiveField> PERSPECTIVE_FIELDS =
      Map.of(
          "me.color",
          new PerspectiveField(
              "CASE WHEN " + ME_IS_WHITE + " THEN 'white' ELSE 'black' END",
              1,
              GroupKind.CATEGORICAL),
          "me.elo",
          new PerspectiveField(
              "CASE WHEN " + ME_IS_WHITE + " THEN white_elo ELSE black_elo END",
              1,
              GroupKind.RATING),
          "me.title",
          new PerspectiveField(
              "CASE WHEN " + ME_IS_WHITE + " THEN white_title ELSE black_title END",
              1,
              GroupKind.CATEGORICAL),
          "opponent.username",
          new PerspectiveField(
              "CASE WHEN " + ME_IS_WHITE + " THEN black_username ELSE white_username END",
              1,
              GroupKind.CATEGORICAL),
          "opponent.elo",
          new PerspectiveField(
              "CASE WHEN " + ME_IS_WHITE + " THEN black_elo ELSE white_elo END",
              1,
              GroupKind.RATING),
          "opponent.title",
          new PerspectiveField(
              "CASE WHEN " + ME_IS_WHITE + " THEN black_title ELSE white_title END",
              1,
              GroupKind.CATEGORICAL),
          "outcome",
          new PerspectiveField(
              "CASE WHEN result = '1/2-1/2' THEN 'draw'"
                  + " WHEN (result = '1-0' AND LOWER(white_username) = LOWER(?))"
                  + " OR (result = '0-1' AND LOWER(black_username) = LOWER(?)) THEN 'win'"
                  + " WHEN result IN ('1-0', '0-1') THEN 'loss' ELSE 'unknown' END",
              2,
              GroupKind.CATEGORICAL));

  /**
   * Every accepted groupBy spelling of a perspective field, mapped to its canonical dotted name.
   * Derived, not hand-typed: the underscore spelling is mechanically the dotted one with {@code
   * .}→{@code _}, so a field added to {@link #PERSPECTIVE_FIELDS} is groupable under both spellings
   * with no second edit site to forget.
   */
  private static final Map<String, String> PERSPECTIVE_SPELLINGS = perspectiveSpellings();

  private static Map<String, String> perspectiveSpellings() {
    Map<String, String> spellings = new LinkedHashMap<>();
    for (String field : PERSPECTIVE_FIELDS.keySet()) {
      spellings.put(field, field);
      spellings.put(field.replace('.', '_'), field);
    }
    return Map.copyOf(spellings);
  }

  /** The groupable-as-is roster for error messages, derived so it cannot drift from the maps. */
  private static final String CATEGORICAL_ROSTER = roster(GroupKind.CATEGORICAL, ", ");

  private static final String RATING_ROSTER = roster(GroupKind.RATING, ", ");

  private static final String RATING_ROSTER_SLASHED = roster(GroupKind.RATING, " / ");

  private static String roster(GroupKind kind, String separator) {
    return PERSPECTIVE_FIELDS.entrySet().stream()
        .filter(e -> e.getValue().kind() == kind)
        .map(Map.Entry::getKey)
        .sorted()
        .collect(Collectors.joining(separator));
  }

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
   * the perspective fields, which are groupable when a player is supplied — the only way to
   * aggregate opponents across both colors, since the color-specific columns mix the player's own
   * values into the buckets on half the rows. The categorical fields ({@code me.color}, {@code
   * me.title}, {@code opponent.username}, {@code opponent.title}, {@code outcome}) group by value;
   * the rating fields ({@code me.elo}, {@code opponent.elo}) group into fixed-width buckets keyed
   * by the band's lower bound, 100 points wide unless the term supplies a width ({@code
   * opponent.elo(200)}).
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
        selectExprs.add(groupTermExpr(term, perspective, selectParams) + " AS " + term.key());
      }
    }

    List<Object> whereParams = new ArrayList<>();
    String whereClause = compileExpr(pq.expr(), whereParams, perspective);
    whereClause = guardParticipation(whereClause, whereParams, perspective);
    requireTheAggregateScopesToItsPlayer(pq, perspective);

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
      groupExprs.add(groupTermExpr(term, perspective, groupParams));
    }
    whereClause = guardParticipation(whereClause, whereParams, perspective);
    requireTheAggregateScopesToItsPlayer(pq, perspective);

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
   * perspective fields resolve to their underscore form ({@code me_color}, {@code opponent_title},
   * {@code outcome}, ...), which is also the SELECT alias aggregate rows are keyed by. Rating
   * bucket terms resolve to the bare underscore name ({@code opponent.elo(200)} → {@code
   * opponent_elo}) — the width shapes the SQL, not the key. Throws on unknown fields.
   */
  public List<String> resolveGroupByColumns(List<String> groupByFields) {
    return resolveGroupByTerms(groupByFields).stream().map(GroupByTerm::key).toList();
  }

  /**
   * One resolved groupBy term: the group key (canonical column name, or underscore perspective name
   * used as the SELECT alias); for perspective terms, the dotted field to render the CASE
   * expression from ({@code null} for physical columns); and for rating terms, the bucket width the
   * rendered expression is floored to ({@code null} for everything else). Rating buckets are
   * half-open bands keyed by their numeric lower bound ({@code (elo / width) * width}) — numeric so
   * the ORDER BY tiebreak sorts bands numerically, where a rendered label like '800-899' would sort
   * lexicographically after '1000-1099'.
   */
  private record GroupByTerm(String key, String perspectiveField, Integer bucketWidth) {
    static GroupByTerm perspective(String field, Integer bucketWidth) {
      return new GroupByTerm(field.replace('.', '_'), field, bucketWidth);
    }

    static GroupByTerm column(String column) {
      return new GroupByTerm(column, null, null);
    }
  }

  private static final int DEFAULT_ELO_BUCKET_WIDTH = 100;

  private List<GroupByTerm> resolveGroupByTerms(List<String> groupByFields) {
    if (groupByFields == null || groupByFields.isEmpty()) {
      throw new IllegalArgumentException("groupBy requires at least one field");
    }
    // Dedupe by group key, first spelling wins — but two different widths for one field would
    // silently keep whichever came first, so that conflict is an error instead.
    Map<String, GroupByTerm> terms = new LinkedHashMap<>();
    for (String field : groupByFields) {
      GroupByTerm term = resolveGroupByTerm(field);
      GroupByTerm existing = terms.putIfAbsent(term.key(), term);
      if (existing != null && !Objects.equals(existing.bucketWidth(), term.bucketWidth())) {
        throw new IllegalArgumentException(
            "Conflicting bucket widths for "
                + term.perspectiveField()
                + ": "
                + existing.bucketWidth()
                + " and "
                + term.bucketWidth()
                + " (bare "
                + RATING_ROSTER_SLASHED
                + " means a width of "
                + DEFAULT_ELO_BUCKET_WIDTH
                + ")");
      }
    }
    return List.copyOf(terms.values());
  }

  private GroupByTerm resolveGroupByTerm(String field) {
    String canonical = PERSPECTIVE_SPELLINGS.get(field);
    if (canonical != null) {
      // Categorical fields group by value; rating fields never group raw — a bare rating term
      // means the default bucket width.
      return switch (PERSPECTIVE_FIELDS.get(canonical).kind()) {
        case CATEGORICAL -> GroupByTerm.perspective(canonical, null);
        case RATING -> GroupByTerm.perspective(canonical, DEFAULT_ELO_BUCKET_WIDTH);
      };
    }
    if (field.indexOf('(') >= 0) {
      return resolveBucketTerm(field);
    }
    if (DATE_FIELD.equals(field) || MONTH_FIELD.equals(field)) {
      throw new IllegalArgumentException(
          "'"
              + field
              + "' is a filter-only field and is not supported in groupBy; use it in the query"
              + " filter instead (e.g. "
              + dateFieldExample(field)
              + ")");
    }
    return GroupByTerm.column(resolveColumn(field));
  }

  /** A parenthesized groupBy term is a bucket width, allowed only on the rating fields. */
  private GroupByTerm resolveBucketTerm(String field) {
    int paren = field.indexOf('(');
    String canonical = PERSPECTIVE_SPELLINGS.get(field.substring(0, paren));
    if (canonical == null || PERSPECTIVE_FIELDS.get(canonical).kind() != GroupKind.RATING) {
      throw new IllegalArgumentException(
          "Only the rating fields take a bucket width in groupBy: "
              + field
              + ". Bucketed: "
              + RATING_ROSTER
              + " (e.g. opponent.elo(200)); groupable as-is, with a player: "
              + CATEGORICAL_ROSTER);
    }
    // An unclosed paren must not lenient-parse "me.elo(100" into a working term; the empty
    // widthText fails the same validation as any other malformed width.
    String widthText = field.endsWith(")") ? field.substring(paren + 1, field.length() - 1) : "";
    int width;
    try {
      width = Integer.parseInt(widthText);
    } catch (NumberFormatException e) {
      throw new IllegalArgumentException(invalidBucketWidth(field));
    }
    if (width < 1) {
      throw new IllegalArgumentException(invalidBucketWidth(field));
    }
    return GroupByTerm.perspective(canonical, width);
  }

  private static String invalidBucketWidth(String field) {
    return "Bucket width must be a positive integer: "
        + field
        + ". Bare "
        + RATING_ROSTER_SLASHED
        + " bucket by "
        + DEFAULT_ELO_BUCKET_WIDTH
        + "; opponent.elo(200) groups ratings into [2000, 2200), [2200, 2400), ...";
  }

  /**
   * Aggregates must not name a player they never scope to.
   *
   * <p>A player only narrows a query through the participation guard, and the guard only fires when
   * a perspective field is in play (it exists to repair the CASE expressions, not to filter). On
   * the select path that is harmless: the rows carry usernames, so a caller who expected one
   * player's games sees strangers immediately. An aggregate has no such tell — {@code player:
   * hikaru, query: num.moves >= 0, group by opening_family} returns the whole corpus's openings
   * under a heading that reads as hikaru's, and nothing in the response reveals it. So the
   * aggregate entry points reject the combination instead of answering the wrong question
   * convincingly.
   *
   * <p>A filter that pins a username column to the named player counts as scoping — the caller said
   * what they meant in the query itself, and the named player is then merely redundant. That is a
   * narrower test than "a username is mentioned somewhere", deliberately: {@code NOT white.username
   * = "magnus"} names a username while saying nothing about the player, and answering it would
   * reproduce exactly the failure this method exists to prevent.
   */
  private void requireTheAggregateScopesToItsPlayer(ParsedQuery pq, Perspective perspective) {
    // Perspective normalizes a blank player to null, so the null check covers both.
    if (perspective.player == null || perspective.used || scopesToPlayer(pq.expr(), perspective)) {
      return;
    }
    throw new IllegalArgumentException(
        "player \""
            + perspective.player
            + "\" would not scope this aggregate: no perspective field ("
            + PERSPECTIVE_FIELDS.keySet().stream().sorted().collect(Collectors.joining(", "))
            + ") appears in the filter or groupBy, and the filter does not restrict a username"
            + " to that player, so the result would cover games that are not theirs. Group by or"
            + " filter on a perspective field, or filter explicitly:"
            + " white.username = \"NAME\" OR black.username = \"NAME\" (the same NAME).");
  }

  /**
   * Whether this filter cannot match a game the named player did not play.
   *
   * <p>Read as "does every row this expression admits belong to the player?", which is why the
   * cases are not symmetric. A conjunction scopes if <em>any</em> conjunct does, since AND only
   * narrows. A disjunction scopes only if <em>every</em> branch does, since one unscoped branch
   * admits the whole corpus. Negation never scopes: the complement of "is the player" is everyone
   * else. Only equality pins a value — {@code !=} and the ordering operators widen — and an {@code
   * IN} list scopes only when every alternative is the player, since a list naming anyone else
   * admits their games too.
   */
  private boolean scopesToPlayer(Expr expr, Perspective perspective) {
    return switch (expr) {
      case OrExpr or -> or.operands().stream().allMatch(e -> scopesToPlayer(e, perspective));
      case AndExpr and -> and.operands().stream().anyMatch(e -> scopesToPlayer(e, perspective));
      case NotExpr ignored -> false;
      case ComparisonExpr cmp ->
          "=".equals(cmp.operator())
              && isUsernameField(cmp.field())
              && isThePlayer(cmp.value(), perspective);
      case InExpr in ->
          isUsernameField(in.field())
              && !in.values().isEmpty()
              && in.values().stream().allMatch(v -> isThePlayer(v, perspective));
      case MotifExpr ignored -> false;
      case SequenceExpr ignored -> false;
    };
  }

  /** Compared case-insensitively, since the compiled username predicates case-fold both sides. */
  private static boolean isThePlayer(Object value, Perspective perspective) {
    return value instanceof String name && name.equalsIgnoreCase(perspective.player);
  }

  private boolean isUsernameField(String field) {
    try {
      String column = resolveColumn(field);
      return column.equals("white_username") || column.equals("black_username");
    } catch (IllegalArgumentException unknownOrPerspective) {
      // Perspective spellings and genuinely unknown fields both land here. The first is already
      // covered by perspective.used; the second is compileExpr's error to report, not ours.
      return false;
    }
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
      if (perspectiveField.kind() == GroupKind.CATEGORICAL && (op.equals("=") || op.equals("!="))) {
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
   * D, and so on.
   *
   * <p>played_at is TIMESTAMP WITHOUT TIME ZONE, i.e. a wall clock, and the wall clock stored there
   * is UTC by convention — so the right parameter is a zone-free {@link LocalDateTime} taken
   * straight from the calendar boundary. Nothing converts between an instant and a wall clock here,
   * so there is no zone for a caller's JVM default to get wrong.
   */
  private static String compileDateComparison(ComparisonExpr cmp, List<Object> params) {
    LocalDateTime start;
    LocalDateTime end;
    if (MONTH_FIELD.equals(cmp.field())) {
      if (!cmp.operator().equals("=")) {
        throw new IllegalArgumentException(
            "month supports only '=' (use date for range comparisons), got: " + cmp.operator());
      }
      YearMonth month = parseMonthValue(cmp.value());
      start = month.atDay(1).atStartOfDay();
      end = month.plusMonths(1).atDay(1).atStartOfDay();
    } else {
      LocalDate day = parseDateValue(cmp.value());
      start = day.atStartOfDay();
      end = day.plusDays(1).atStartOfDay();
    }
    switch (cmp.operator()) {
      case "=":
        params.add(start);
        params.add(end);
        return "(played_at >= ? AND played_at < ?)";
      case "!=":
        params.add(start);
        params.add(end);
        return "(played_at < ? OR played_at >= ?)";
      case "<":
        params.add(start);
        return "played_at < ?";
      case ">=":
        params.add(start);
        return "played_at >= ?";
      case "<=":
        params.add(end);
        return "played_at < ?";
      case ">":
        params.add(end);
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

  /** The canonical single-value example for a date-scoping field, used in error messages. */
  private static String dateFieldExample(String field) {
    return MONTH_FIELD.equals(field) ? "month = \"2026-07\"" : "date >= \"2026-07-01\"";
  }

  private String compileIn(InExpr in, List<Object> params, Perspective perspective) {
    if (DATE_FIELD.equals(in.field()) || MONTH_FIELD.equals(in.field())) {
      throw new IllegalArgumentException(
          in.field()
              + " does not support IN; use comparisons instead ("
              + dateFieldExample(in.field())
              + ", or a range like date >= \"2026-07-01\" AND date < \"2026-09-01\")");
    }
    PerspectiveField perspectiveField = PERSPECTIVE_FIELDS.get(in.field());
    if (perspectiveField != null) {
      String expr = perspectiveExpr(in.field(), perspectiveField, perspective, params);
      params.addAll(in.values());
      if (perspectiveField.kind() == GroupKind.CATEGORICAL) {
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

  /**
   * Renders a groupBy term's SQL: the bare column name for physical terms, the CASE expression
   * (binding its player params in SQL order) for perspective terms. Rating terms floor the CASE to
   * their bucket's lower bound with integer arithmetic — {@code / width * width} truncates
   * identically on H2 and Postgres for the non-negative INT elo columns, and a NULL elo propagates
   * straight through to the NULL group bucket. The width is an inlined literal rather than a bind
   * param (it round-trips through Integer.parseInt, so there is no injection surface) to keep the
   * groups/totals param positions identical to the categorical fields'.
   */
  private static String groupTermExpr(
      GroupByTerm term, Perspective perspective, List<Object> params) {
    if (term.perspectiveField() == null) {
      return term.key();
    }
    String expr =
        perspectiveExpr(
            term.perspectiveField(),
            PERSPECTIVE_FIELDS.get(term.perspectiveField()),
            perspective,
            params);
    if (term.bucketWidth() == null) {
      return expr;
    }
    return expr + " / " + term.bucketWidth() + " * " + term.bucketWidth();
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
