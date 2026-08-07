package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.dto.GameFeature;
import java.io.Closeable;
import java.net.URI;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.Statement;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import javax.sql.DataSource;
import org.jdbi.v3.core.Jdbi;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * The username expression indexes on the deployment dialect. The participation guard case-folds
 * both sides — {@code LOWER(white_username) = LOWER(?)} — so only an expression index on {@code
 * LOWER(...)} can serve it, and only Postgres has expression indexes; the H2 side of the migration
 * creates plain-column stand-ins that this predicate cannot use. The compiled predicate shape is
 * pinned on H2 too ({@code MigrationTest}), so what is uniquely this suite's job is the
 * <em>index</em> side of the contract: an index expression drifting from the compiler's predicate —
 * losing its {@code LOWER}, say — leaves every H2 test green while production quietly returns to
 * the full-table walk these indexes exist to remove (#1313 item 10).
 *
 * <p>Runs against the real postgres CI provides via {@code PG_TEST_DB_URL}; skips when unset. Uses
 * a dedicated schema like the other PG-gated suites sharing that scratch database.
 */
public class PostgresPlayerIndexTest {

  private static final String DB_URL_ENV = "PG_TEST_DB_URL";
  private static final String SCHEMA = "one_d4_pg_player_index_test";

  private DataSource dataSource;
  private GameFeatureDao dao;
  private UUID requestId;

  @BeforeEach
  public void setUp() throws Exception {
    String rawUrl = System.getenv(DB_URL_ENV);
    assumeTrue(
        rawUrl != null && !rawUrl.isBlank(),
        DB_URL_ENV + " is not set; skipping the real-postgres player-index suite");

    try (Connection conn = DriverManager.getConnection(jdbcUrl(rawUrl, null));
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP SCHEMA IF EXISTS " + SCHEMA + " CASCADE");
      stmt.execute("CREATE SCHEMA " + SCHEMA);
    }

    dataSource = DataSourceFactory.create(jdbcUrl(rawUrl, SCHEMA));
    new Migration(dataSource, false).run();
    dao = new GameFeatureDao(Jdbi.create(dataSource), false);

    requestId = UUID.randomUUID();
    try (Connection conn = dataSource.getConnection();
        var stmt =
            conn.prepareStatement(
                "INSERT INTO indexing_requests (id, player, platform, start_month, end_month,"
                    + " status) VALUES (?, 'p', 'CHESS_COM', '2026-06', '2026-07', 'COMPLETED')")) {
      stmt.setObject(1, requestId);
      stmt.executeUpdate();
    }
  }

  @AfterEach
  public void tearDown() throws Exception {
    if (dataSource instanceof Closeable closeable) {
      closeable.close();
    }
    String rawUrl = System.getenv(DB_URL_ENV);
    if (rawUrl == null || rawUrl.isBlank()) {
      return;
    }
    try (Connection conn = DriverManager.getConnection(jdbcUrl(rawUrl, null));
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP SCHEMA IF EXISTS " + SCHEMA + " CASCADE");
    }
  }

  /**
   * The plan-level pin: Postgres must answer the compiler's participation guard through both
   * expression indexes — a BitmapOr of the white-side and black-side scans — rather than a
   * sequential scan. {@code enable_seqscan = off} removes the planner's row-count judgement from
   * the test (an empty-ish table would otherwise always seq-scan), leaving exactly the question
   * this suite exists to answer: <em>can</em> the plan reach these indexes for this predicate? With
   * a raw-column index, or a predicate whose shape no longer matches the indexed expression, it
   * cannot — Postgres then seq-scans even at prohibitive cost, and the assertion fails.
   *
   * <p>Deliberately the bare SELECT, without the {@code LIMIT ? OFFSET ?} the DAO appends: with a
   * LIMIT the planner may legitimately prefer walking {@code idx_game_features_played_at} in order
   * and filtering (cheapest-startup wins for a common player), and which side of that coin it picks
   * depends on row statistics this fixture cannot meaningfully supply. Reachability is the stable,
   * load-bearing property; the planner's per-query choice is its own business.
   */
  @Test
  public void thePlayerScopedQueryIsServedByBothUsernameIndexesOnPostgres() throws Exception {
    String plan = explain(new SqlCompiler().compile(Parser.parse("me.elo >= 0"), "hikaru"));

    assertThat(plan)
        .as("the white side of the OR must reach its index")
        .contains("idx_game_features_white_username");
    assertThat(plan)
        .as("and the black side its own — one index per disjunct")
        .contains("idx_game_features_black_username");
    assertThat(plan)
        .as("and nothing may fall back to walking the table")
        .doesNotContain("Seq Scan");
  }

  /**
   * The browse UI's username search — {@code white.username = "x" OR black.username = "x"}, sent by
   * GamesView with no {@code player} — reaches the same predicate shape through a different
   * compiler path (the STRING_COLUMNS equality branch, not the participation guard), and it is the
   * highest-traffic consumer of these indexes. Pinned separately so that path losing its {@code
   * LOWER} cannot fall off the indexes while every guard-path test stays green.
   */
  @Test
  public void theBrowseUsernameSearchIsServedByBothUsernameIndexesOnPostgres() throws Exception {
    String plan =
        explain(
            new SqlCompiler()
                .compile(
                    Parser.parse("white.username = \"hikaru\" OR black.username = \"hikaru\""),
                    null));

    assertThat(plan)
        .as("the UI's search predicate must reach the white-side index")
        .contains("idx_game_features_white_username");
    assertThat(plan).as("and the black-side index").contains("idx_game_features_black_username");
    assertThat(plan)
        .as("and nothing may fall back to walking the table")
        .doesNotContain("Seq Scan");
  }

  private String explain(CompiledQuery compiled) throws Exception {
    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement()) {
      stmt.execute("SET enable_seqscan = off");
      StringBuilder sb = new StringBuilder();
      try (ResultSet rs = stmt.executeQuery("EXPLAIN " + inlineParams(compiled))) {
        while (rs.next()) {
          sb.append(rs.getString(1)).append('\n');
        }
      } finally {
        // Best-effort: this pool dies with the test (fresh DataSource per method), and a reset
        // failure here must not replace the EXPLAIN failure that is the actual signal.
        try {
          stmt.execute("SET enable_seqscan = on");
        } catch (java.sql.SQLException ignored) {
          // The connection is already broken; the primary exception is propagating.
        }
      }
      return sb.toString();
    }
  }

  /**
   * The behavioral twin: the guard is case-insensitive on both sides, so a player search must find
   * games where the stored username differs from the queried one only by case, on both colors,
   * while excluding everyone else — through the real DAO on the deployment dialect. (At three
   * unanalyzed rows the planner seq-scans, so this pins predicate semantics and PG collation
   * agreement, not index participation — that is the plan test's job.)
   */
  @Test
  public void playerSearchIsCaseInsensitiveOnBothSidesOnPostgres() {
    dao.insertBatch(
        List.of(
            game("pgi-1", "Hikaru", "someone"),
            game("pgi-2", "other", "HIKARU"),
            game("pgi-3", "magnus", "fabiano")));

    List<GameFeature> games =
        dao.query(new SqlCompiler().compile(Parser.parse("me.elo >= 0"), "hikaru"), 10, 0);

    assertThat(games)
        .extracting(GameFeature::gameUrl)
        .containsExactlyInAnyOrder("https://chess.com/game/pgi-1", "https://chess.com/game/pgi-2");

    // The browse UI's search path must agree: same predicate shape, different compiler branch.
    List<GameFeature> browsed =
        dao.query(
            new SqlCompiler()
                .compile(
                    Parser.parse("white.username = \"hikaru\" OR black.username = \"hikaru\""),
                    null),
            10,
            0);
    assertThat(browsed)
        .extracting(GameFeature::gameUrl)
        .containsExactlyInAnyOrder("https://chess.com/game/pgi-1", "https://chess.com/game/pgi-2");
  }

  /**
   * Inlines the compiled query's bind params as SQL literals, for EXPLAIN. Placeholders are
   * consumed left to right with a moving cursor rather than repeated replaceFirst from the start,
   * so a parameter value containing {@code ?} can never be mistaken for the next placeholder.
   */
  private static String inlineParams(CompiledQuery compiled) {
    StringBuilder sql = new StringBuilder(compiled.selectSql());
    int cursor = 0;
    for (Object param : compiled.parameters()) {
      String literal =
          param instanceof Number
              ? param.toString()
              : "'" + param.toString().replace("'", "''") + "'";
      int placeholder = sql.indexOf("?", cursor);
      assertThat(placeholder).as("a placeholder must exist for every parameter").isNotNegative();
      sql.replace(placeholder, placeholder + 1, literal);
      cursor = placeholder + literal.length();
    }
    return sql.toString();
  }

  private GameFeature game(String urlSuffix, String white, String black) {
    return new GameFeature(
        null,
        requestId,
        "https://chess.com/game/" + urlSuffix,
        "CHESS_COM",
        white,
        black,
        1500,
        1500,
        null,
        null,
        "blitz",
        "A00",
        "Sicilian",
        "Sicilian",
        "1-0",
        Instant.parse("2026-07-02T10:00:00Z"),
        30,
        Instant.now(),
        "1. e4 e5 *");
  }

  /**
   * Converts the libpq-style URL CI exports ({@code postgresql://user:pass@host:port/db}) into a
   * pgjdbc URL, same as the other PG-gated suites.
   */
  private static String jdbcUrl(String rawUrl, String schema) {
    URI uri = URI.create(rawUrl);
    List<String> params = new ArrayList<>();
    String userInfo = uri.getUserInfo();
    if (userInfo != null) {
      int colon = userInfo.indexOf(':');
      String user = colon < 0 ? userInfo : userInfo.substring(0, colon);
      params.add("user=" + encode(user));
      if (colon >= 0) {
        params.add("password=" + encode(userInfo.substring(colon + 1)));
      }
    }
    if (schema != null) {
      params.add("currentSchema=" + encode(schema));
    }
    int port = uri.getPort() < 0 ? 5432 : uri.getPort();
    return "jdbc:postgresql://"
        + uri.getHost()
        + ":"
        + port
        + uri.getPath()
        + (params.isEmpty() ? "" : "?" + String.join("&", params));
  }

  private static String encode(String value) {
    return URLEncoder.encode(value, StandardCharsets.UTF_8);
  }
}
