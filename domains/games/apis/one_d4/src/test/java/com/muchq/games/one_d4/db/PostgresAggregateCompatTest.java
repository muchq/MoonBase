package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

import com.muchq.games.chessql.compiler.AggregateSpec;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.ParsedQuery;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.dto.AggregateRow;
import com.muchq.games.one_d4.api.dto.GameFeature;
import java.io.Closeable;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.Statement;
import java.time.Instant;
import java.util.List;
import java.util.UUID;
import javax.sql.DataSource;
import org.jdbi.v3.core.Jdbi;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * The aggregate SQL is the part of the compiler where H2 and Postgres can legitimately disagree,
 * and Postgres is the deployment target while every other DAO test runs on H2. Two constructs carry
 * real dialect risk:
 *
 * <ul>
 *   <li>GROUP BY (and the ORDER BY tiebreak) referencing a SELECT-list <em>alias</em> rather than
 *       repeating the perspective CASE expression. Postgres resolves a bare GROUP BY name against
 *       input columns first and output column names second, so this only works because no
 *       game_features column shares the alias. The rating buckets ride the same convention with
 *       integer arithmetic on top of the CASE.
 *   <li>date/month bounds bound as {@link java.time.LocalDateTime} against a TIMESTAMP-without-zone
 *       column. {@link GameFeatureDao} uses that zone-free type on both the write and the read, so
 *       pgjdbc stores the UTC wall clock as-is instead of converting through the JVM default zone;
 *       {@link PlayedAtTimeZoneTest} pins that under a non-UTC JVM on H2, and this suite checks
 *       pgjdbc honours it the same way.
 * </ul>
 *
 * <p>Runs against the real postgres that CI's build-and-test job provides via {@code
 * PG_TEST_DB_URL}; skips when that is unset (i.e. on a developer machine without one). Uses a
 * dedicated schema so it cannot collide with the other suites sharing that scratch database.
 */
public class PostgresAggregateCompatTest {

  private static final String DB_URL_ENV = "PG_TEST_DB_URL";
  private static final String SCHEMA = "one_d4_pg_compat_test";

  private DataSource dataSource;
  private GameFeatureDao dao;
  private UUID requestId;

  @BeforeEach
  public void setUp() throws Exception {
    String rawUrl = System.getenv(DB_URL_ENV);
    assumeTrue(
        rawUrl != null && !rawUrl.isBlank(),
        DB_URL_ENV + " is not set; skipping the real-postgres compatibility suite");

    // A clean schema per run: the same scratch database backs the other DB-gated suites.
    try (Connection conn = DriverManager.getConnection(PgTestUrls.jdbcUrl(rawUrl, null));
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP SCHEMA IF EXISTS " + SCHEMA + " CASCADE");
      stmt.execute("CREATE SCHEMA " + SCHEMA);
    }

    dataSource = DataSourceFactory.create(PgTestUrls.jdbcUrl(rawUrl, SCHEMA));
    new Migration(dataSource, new PostgresSqlDialect()).run();
    dao = new GameFeatureDao(Jdbi.create(dataSource), new PostgresSqlDialect());

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
    try (Connection conn = DriverManager.getConnection(PgTestUrls.jdbcUrl(rawUrl, null));
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP SCHEMA IF EXISTS " + SCHEMA + " CASCADE");
    }
  }

  /**
   * The portability claim behind aliasing the perspective CASE in the SELECT list: Postgres must
   * accept that alias in both GROUP BY and the ORDER BY tiebreak, and must group by the CASE rather
   * than by anything else.
   */
  @Test
  public void aggregateGroupsByPerspectiveAliasOnPostgres() {
    dao.insertBatch(
        List.of(
            game("pg-1", "hikaru", "a", "1-0", "Caro Kann", Instant.parse("2026-07-02T10:00:00Z")),
            game("pg-2", "hikaru", "b", "1-0", "Sicilian", Instant.parse("2026-07-03T10:00:00Z")),
            game("pg-3", "c", "hikaru", "0-1", "English", Instant.parse("2026-07-04T10:00:00Z")),
            game("pg-4", "d", "hikaru", "1-0", "English", Instant.parse("2026-07-05T10:00:00Z")),
            // not hikaru's game: excluded by the participation guard
            game("pg-5", "x", "y", "1-0", "English", Instant.parse("2026-07-06T10:00:00Z"))));

    SqlCompiler compiler = new SqlCompiler();
    ParsedQuery parsed = Parser.parse("time.class = \"blitz\"");
    List<String> groupBy = List.of("me.color", "outcome");
    List<AggregateRow> groups =
        dao.aggregate(
            compiler.compileAggregate(parsed, groupBy, "hikaru"),
            compiler.resolveGroupByColumns(groupBy),
            10);

    assertThat(groups).hasSize(3);
    assertThat(groups.get(0).group())
        .containsEntry("me_color", "white")
        .containsEntry("outcome", "win");
    assertThat(groups.get(0).count()).isEqualTo(2);
    assertThat(groups.get(1).group())
        .containsEntry("me_color", "black")
        .containsEntry("outcome", "loss");
    assertThat(groups.get(2).group())
        .containsEntry("me_color", "black")
        .containsEntry("outcome", "win");

    var totals = dao.aggregateTotals(compiler.compileAggregateTotals(parsed, groupBy, "hikaru"));
    assertThat(totals.totalGames()).isEqualTo(4);
    assertThat(totals.totalGroups()).isEqualTo(3);

    // Mixing an output alias with an input column in one GROUP BY / ORDER BY is where the
    // resolution rules would bite if they differed per term.
    List<String> mixed = List.of("me.color", "opening_family");
    List<AggregateRow> mixedGroups =
        dao.aggregate(
            compiler.compileAggregate(parsed, mixed, "hikaru"),
            compiler.resolveGroupByColumns(mixed),
            10);
    assertThat(
            mixedGroups.stream()
                .map(g -> g.group().get("me_color") + "/" + g.group().get("opening_family")))
        .containsExactlyInAnyOrder("white/Caro Kann", "white/Sicilian", "black/English");
    var mixedTotals = dao.aggregateTotals(compiler.compileAggregateTotals(parsed, mixed, "hikaru"));
    assertThat(mixedTotals.totalGames()).isEqualTo(4);
    assertThat(mixedTotals.totalGroups()).isEqualTo(3);
  }

  /**
   * Grouping by opponent.title on real Postgres resolves the opposite side's nullable column, so
   * this doubles as the pin that a NULL group key groups (Postgres pools NULLs into one GROUP BY
   * bucket, like H2) and comes back as a null map value rather than an error.
   */
  @Test
  public void aggregateGroupsByOpponentTitleWithNullBucketOnPostgres() {
    dao.insertBatch(
        List.of(
            game(
                "pgt-1",
                "hikaru",
                "gmfoe",
                null,
                "GM",
                "1-0",
                "Caro Kann",
                Instant.parse("2026-07-02T10:00:00Z")),
            game(
                "pgt-2",
                "gmfoe2",
                "hikaru",
                "GM",
                null,
                "1-0",
                "Caro Kann",
                Instant.parse("2026-07-03T10:00:00Z")),
            game(
                "pgt-3",
                "plain",
                "hikaru",
                null,
                null,
                "1-0",
                "Caro Kann",
                Instant.parse("2026-07-04T10:00:00Z"))));

    SqlCompiler compiler = new SqlCompiler();
    ParsedQuery parsed = Parser.parse("time.class = \"blitz\"");
    List<String> groupBy = List.of("opponent.title");
    List<AggregateRow> groups =
        dao.aggregate(
            compiler.compileAggregate(parsed, groupBy, "hikaru"),
            compiler.resolveGroupByColumns(groupBy),
            10);

    assertThat(groups).hasSize(2);
    assertThat(groups.get(0).group()).containsEntry("opponent_title", "GM");
    assertThat(groups.get(0).count()).isEqualTo(2);
    assertThat(groups.get(1).group()).containsEntry("opponent_title", null);
    assertThat(groups.get(1).count()).isEqualTo(1);

    var totals = dao.aggregateTotals(compiler.compileAggregateTotals(parsed, groupBy, "hikaru"));
    assertThat(totals.totalGames()).isEqualTo(3);
    assertThat(totals.totalGroups()).isEqualTo(2);
  }

  /**
   * The bucket arithmetic on real Postgres: {@code (CASE ...) / width * width} under a SELECT alias
   * that GROUP BY and the tiebreak reference. Integer division must truncate the same way H2's does
   * (INT / INT stays INT — a dialect that widened to numeric would surface here as a non-integer
   * key), a NULL elo must propagate through the arithmetic into the NULL bucket, and pgjdbc must
   * hand the key back as an Integer.
   */
  @Test
  public void aggregateGroupsByOpponentEloBucketsWithNullBucketOnPostgres() {
    dao.insertBatch(
        List.of(
            game(
                "pge-1",
                "hikaru",
                "a",
                2800,
                2450,
                null,
                null,
                "1-0",
                "Caro Kann",
                Instant.parse("2026-07-02T10:00:00Z")),
            game(
                "pge-2",
                "b",
                "hikaru",
                2499,
                2800,
                null,
                null,
                "1-0",
                "Caro Kann",
                Instant.parse("2026-07-03T10:00:00Z")),
            game(
                "pge-3",
                "hikaru",
                "c",
                2800,
                2399,
                null,
                null,
                "1-0",
                "Caro Kann",
                Instant.parse("2026-07-04T10:00:00Z")),
            game(
                "pge-4",
                "d",
                "hikaru",
                null,
                2800,
                null,
                null,
                "1-0",
                "Caro Kann",
                Instant.parse("2026-07-05T10:00:00Z"))));

    SqlCompiler compiler = new SqlCompiler();
    ParsedQuery parsed = Parser.parse("time.class = \"blitz\"");
    List<String> groupBy = List.of("opponent.elo");
    List<AggregateRow> groups =
        dao.aggregate(
            compiler.compileAggregate(parsed, groupBy, "hikaru"),
            compiler.resolveGroupByColumns(groupBy),
            10);

    // 2450 (hikaru as White) and 2499 (as Black) pool into [2400, 2500); 2399 and NULL each get
    // their own bucket. The count-1 groups tie, so their assertions are order-free.
    assertThat(groups).hasSize(3);
    assertThat(groups.get(0).group()).containsEntry("opponent_elo", 2400);
    assertThat(groups.get(0).count()).isEqualTo(2);
    assertThat(groups)
        .anySatisfy(
            g -> {
              assertThat(g.group()).containsEntry("opponent_elo", 2300);
              assertThat(g.count()).isEqualTo(1);
            })
        .anySatisfy(
            g -> {
              assertThat(g.group()).containsEntry("opponent_elo", null);
              assertThat(g.count()).isEqualTo(1);
            });

    var totals = dao.aggregateTotals(compiler.compileAggregateTotals(parsed, groupBy, "hikaru"));
    assertThat(totals.totalGames()).isEqualTo(4);
    assertThat(totals.totalGroups()).isEqualTo(3);

    // Caller-supplied width, same alias convention: at 200 wide 2399 joins [2200, 2400).
    List<String> wideBy = List.of("opponent.elo(200)");
    List<AggregateRow> wide =
        dao.aggregate(
            compiler.compileAggregate(parsed, wideBy, "hikaru"),
            compiler.resolveGroupByColumns(wideBy),
            10);
    assertThat(wide.get(0).group()).containsEntry("opponent_elo", 2400);
    assertThat(wide.get(0).count()).isEqualTo(2);
    assertThat(wide).anySatisfy(g -> assertThat(g.group()).containsEntry("opponent_elo", 2200));
  }

  /**
   * The other half of the dialect divergence pinned by
   * GameFeatureDaoTest.aggregate_nullGroupKeySortsFirstInTheTiebreakOnH2: on Postgres a NULL group
   * key sorts LAST in the ASC tiebreak. The two twins pin opposite orders on purpose — the compiler
   * emits no NULLS FIRST/LAST normalization, and this pair is what turns that recorded divergence
   * into something CI checks instead of something comments assert.
   */
  @Test
  public void nullGroupKeySortsLastInTheTiebreakOnPostgres() {
    dao.insertBatch(
        List.of(
            game(
                "pgn-1",
                "hikaru",
                "fmfoe",
                null,
                "FM",
                "1-0",
                "Caro Kann",
                Instant.parse("2026-07-02T10:00:00Z")),
            game(
                "pgn-2",
                "untitled_foe",
                "hikaru",
                null,
                null,
                "1-0",
                "Caro Kann",
                Instant.parse("2026-07-03T10:00:00Z"))));

    SqlCompiler compiler = new SqlCompiler();
    List<AggregateRow> groups =
        dao.aggregate(
            compiler.compileAggregate(
                Parser.parse("time.class = \"blitz\""), List.of("opponent.title"), "hikaru"),
            List.of("opponent_title"),
            10);

    // Both groups tie at count 1, so the order IS the ASC tiebreak — and on Postgres the NULL
    // key trails.
    assertThat(groups.stream().map(g -> g.group().get("opponent_title")))
        .containsExactly("FM", null);
  }

  /**
   * The bucket arithmetic at the INT extremes on the real dialect — the H2 twin
   * (GameFeatureDaoTest.aggregate_bucketArithmeticAtIntegerExtremes) explains the fixture. This is
   * the test behind the portability claim that {@code int4 / int4 * int4} neither widens nor raises
   * for any elo the column can hold: Integer.MAX_VALUE at width 100 must key 2147483600 as an
   * Integer, a negative elo must truncate toward zero (-150 → -100, where FLOOR would give -200),
   * and a width of Integer.MAX_VALUE must collapse smaller elos to bucket 0 while keying the
   * MAX_VALUE elo as itself.
   */
  @Test
  public void aggregateBucketArithmeticAtIntegerExtremesOnPostgres() {
    dao.insertBatch(
        List.of(
            game(
                "pgx-1",
                "hikaru",
                "a",
                2800,
                2450,
                null,
                null,
                "1-0",
                "Caro Kann",
                Instant.parse("2026-07-02T10:00:00Z")),
            game(
                "pgx-2",
                "hikaru",
                "b",
                2800,
                Integer.MAX_VALUE,
                null,
                null,
                "1-0",
                "Caro Kann",
                Instant.parse("2026-07-03T10:00:00Z")),
            game(
                "pgx-3",
                "hikaru",
                "c",
                2800,
                -150,
                null,
                null,
                "1-0",
                "Caro Kann",
                Instant.parse("2026-07-04T10:00:00Z"))));

    SqlCompiler compiler = new SqlCompiler();
    ParsedQuery parsed = Parser.parse("time.class = \"blitz\"");
    List<String> groupBy = List.of("opponent.elo");
    List<AggregateRow> groups =
        dao.aggregate(
            compiler.compileAggregate(parsed, groupBy, "hikaru"),
            compiler.resolveGroupByColumns(groupBy),
            10);

    assertThat(groups).hasSize(3);
    assertThat(groups)
        .anySatisfy(g -> assertThat(g.group()).containsEntry("opponent_elo", 2400))
        .anySatisfy(g -> assertThat(g.group()).containsEntry("opponent_elo", 2147483600))
        .anySatisfy(g -> assertThat(g.group()).containsEntry("opponent_elo", -100));

    List<String> maxWidth = List.of("opponent.elo(" + Integer.MAX_VALUE + ")");
    List<AggregateRow> collapsed =
        dao.aggregate(
            compiler.compileAggregate(parsed, maxWidth, "hikaru"),
            compiler.resolveGroupByColumns(maxWidth),
            10);
    assertThat(collapsed).hasSize(2);
    assertThat(collapsed.get(0).group()).containsEntry("opponent_elo", 0);
    assertThat(collapsed.get(0).count()).isEqualTo(2);
    assertThat(collapsed.get(1).group()).containsEntry("opponent_elo", Integer.MAX_VALUE);
    assertThat(collapsed.get(1).count()).isEqualTo(1);
  }

  /**
   * The totals query inlines the perspective CASE in GROUP BY while the groups query aliases it, so
   * the two statements bind their shared params in different positions. If either mapping were
   * wrong the counts would silently disagree rather than error.
   */
  @Test
  public void aggregateTotalsMatchTheGroupsTheySummarizeOnPostgres() {
    dao.insertBatch(
        List.of(
            game("tp-1", "w", "b", "1-0", "Caro Kann", Instant.parse("2026-07-02T10:00:00Z")),
            game("tp-2", "w", "b", "1-0", "Caro Kann", Instant.parse("2026-07-03T10:00:00Z")),
            game("tp-3", "w", "b", "1-0", "Sicilian", Instant.parse("2026-07-04T10:00:00Z")),
            // NULL opening_family forms its own group rather than vanishing
            game("tp-4", "w", "b", "1-0", null, Instant.parse("2026-07-05T10:00:00Z")),
            // out of the date scope, so it must not reach either total
            game("tp-5", "w", "b", "1-0", "English", Instant.parse("2026-06-30T23:59:59Z"))));

    SqlCompiler compiler = new SqlCompiler();
    ParsedQuery parsed = Parser.parse("month = \"2026-07\"");
    List<AggregateRow> groups =
        dao.aggregate(
            compiler.compileAggregate(parsed, List.of("opening_family")),
            List.of("opening_family"),
            50);
    var totals =
        dao.aggregateTotals(compiler.compileAggregateTotals(parsed, List.of("opening_family")));

    assertThat(groups.stream().map(g -> g.group().get("opening_family")))
        .containsExactlyInAnyOrder("Caro Kann", "Sicilian", null);
    assertThat(groups.stream().mapToLong(AggregateRow::count).sum()).isEqualTo(4);
    assertThat(totals.totalGames()).isEqualTo(4);
    assertThat(totals.totalGroups()).isEqualTo(3);
  }

  /**
   * The outcome metrics and the score ranking on the real dialect (#1345). Three things could
   * differ from H2 and none of them would show up as a compile error: {@code SUM(CASE ... THEN 1
   * ELSE 0 END)} comes back as bigint rather than int, the ranking wraps the grouped query in a
   * derived table whose columns the outer ORDER BY names, and {@code (wins * 2 + draws) * 1.0 /
   * group_count} is numeric division in Postgres and floating-point in H2 — which must still order
   * the same way. The floor rides along as a HAVING inside the derived table.
   */
  @Test
  public void outcomeMetricsAndScoreRankingOnPostgres() {
    dao.insertBatch(
        List.of(
            // Sideline: one win, a perfect rate on no evidence — excluded by the floor.
            game("sc-1", "hikaru", "a", "1-0", "Side", Instant.parse("2026-07-02T10:00:00Z")),
            // Caro: a win as White, a win as Black, a draw, a loss, and an unfinished game.
            game("sc-2", "hikaru", "b", "1-0", "Caro", Instant.parse("2026-07-03T10:00:00Z")),
            game("sc-3", "c", "hikaru", "0-1", "Caro", Instant.parse("2026-07-04T10:00:00Z")),
            game("sc-4", "hikaru", "d", "1/2-1/2", "Caro", Instant.parse("2026-07-05T10:00:00Z")),
            game("sc-5", "e", "hikaru", "1-0", "Caro", Instant.parse("2026-07-06T10:00:00Z")),
            game("sc-6", "hikaru", "f", "*", "Caro", Instant.parse("2026-07-07T10:00:00Z")),
            // Sicilian: a win and a draw — the best real rate.
            game("sc-7", "hikaru", "g", "1-0", "Sic", Instant.parse("2026-07-08T10:00:00Z")),
            game("sc-8", "hikaru", "h", "1/2-1/2", "Sic", Instant.parse("2026-07-09T10:00:00Z"))));

    SqlCompiler compiler = new SqlCompiler();
    ParsedQuery parsed = Parser.parse("white.username = \"hikaru\" OR black.username = \"hikaru\"");
    List<String> groupBy = List.of("opening_family");

    AggregateSpec metrics = AggregateSpec.of(groupBy, "hikaru");
    List<AggregateRow> byCount =
        dao.aggregate(
            compiler.compileAggregate(parsed, metrics),
            compiler.resolveGroupByColumns(groupBy),
            metrics.hasOutcomeMetrics(),
            50);

    AggregateRow caro =
        byCount.stream()
            .filter(g -> "Caro".equals(g.group().get("opening_family")))
            .findFirst()
            .orElseThrow();
    assertThat(caro.count()).isEqualTo(5);
    assertThat(caro.wins()).isEqualTo(2);
    assertThat(caro.losses()).isEqualTo(1);
    assertThat(caro.draws()).isEqualTo(1);
    assertThat(caro.score()).isEqualTo(2.5);

    AggregateSpec ranked = new AggregateSpec(groupBy, "hikaru", AggregateSpec.Order.SCORE, 2);
    List<AggregateRow> byScore =
        dao.aggregate(
            compiler.compileAggregate(parsed, ranked),
            compiler.resolveGroupByColumns(groupBy),
            ranked.hasOutcomeMetrics(),
            50);

    assertThat(byScore.stream().map(g -> g.group().get("opening_family")))
        .as("ranked by points per game, with the one-game sideline below the floor")
        .containsExactly("Sic", "Caro");

    var totals = dao.aggregateTotals(compiler.compileAggregateTotals(parsed, ranked));
    assertThat(totals.totalGroups()).isEqualTo(2);
    assertThat(totals.totalGames()).isEqualTo(7);
  }

  /** The same day/month boundary math the H2 suite pins, against pgjdbc's timestamp binding. */
  @Test
  public void dateAndMonthBoundariesOnPostgres() {
    dao.insertBatch(
        List.of(
            game("prevEnd", "w", "b", "1-0", null, Instant.parse("2026-06-14T23:59:59.999Z")),
            game("dayStart", "w", "b", "1-0", null, Instant.parse("2026-06-15T00:00:00.000Z")),
            game("dayEnd", "w", "b", "1-0", null, Instant.parse("2026-06-15T23:59:59.999Z")),
            game("nextStart", "w", "b", "1-0", null, Instant.parse("2026-06-16T00:00:00.000Z"))));

    assertThat(urlsMatching("date = \"2026-06-15\"")).containsExactly("dayEnd", "dayStart");
    assertThat(urlsMatching("date != \"2026-06-15\"")).containsExactly("nextStart", "prevEnd");
    assertThat(urlsMatching("date < \"2026-06-15\"")).containsExactly("prevEnd");
    assertThat(urlsMatching("date <= \"2026-06-15\""))
        .containsExactly("dayEnd", "dayStart", "prevEnd");
    assertThat(urlsMatching("date > \"2026-06-15\"")).containsExactly("nextStart");
    assertThat(urlsMatching("date >= \"2026-06-15\""))
        .containsExactly("dayEnd", "dayStart", "nextStart");
    assertThat(urlsMatching("month = \"2026-06\""))
        .containsExactly("dayEnd", "dayStart", "nextStart", "prevEnd");
  }

  /**
   * A perspective filter, a date bound and the motif_count ORDER BY each contribute bind params at
   * different points in the statement. Postgres rejects a placeholder whose inferred type does not
   * match the value, so a misordered list fails here even when H2 would coerce it.
   */
  @Test
  public void mixedParamOrderExecutesOnPostgres() {
    dao.insertBatch(
        List.of(
            game("mo-1", "hikaru", "a", "1-0", "Caro Kann", Instant.parse("2026-07-02T10:00:00Z")),
            game("mo-2", "b", "hikaru", "1-0", "Sicilian", Instant.parse("2026-07-03T10:00:00Z"))));

    List<String> urls =
        urlsMatching(
            "outcome = \"win\" AND date >= \"2026-07-01\" ORDER BY motif_count(pin) DESC",
            "hikaru");
    assertThat(urls).containsExactly("mo-1");
  }

  private List<String> urlsMatching(String chessql) {
    return urlsMatching(chessql, null);
  }

  private List<String> urlsMatching(String chessql, String player) {
    return dao.query(new SqlCompiler().compile(Parser.parse(chessql), player), 50, 0).stream()
        .map(GameFeature::gameUrl)
        .sorted()
        .toList();
  }

  private GameFeature game(
      String url,
      String white,
      String black,
      String result,
      String openingFamily,
      Instant playedAt) {
    return game(url, white, black, null, null, result, openingFamily, playedAt);
  }

  private GameFeature game(
      String url,
      String white,
      String black,
      String whiteTitle,
      String blackTitle,
      String result,
      String openingFamily,
      Instant playedAt) {
    return game(
        url, white, black, 1500, 1500, whiteTitle, blackTitle, result, openingFamily, playedAt);
  }

  private GameFeature game(
      String url,
      String white,
      String black,
      Integer whiteElo,
      Integer blackElo,
      String whiteTitle,
      String blackTitle,
      String result,
      String openingFamily,
      Instant playedAt) {
    return new GameFeature(
        null,
        requestId,
        url,
        "CHESS_COM",
        white,
        black,
        whiteElo,
        blackElo,
        whiteTitle,
        blackTitle,
        "blitz",
        "A00",
        openingFamily,
        openingFamily,
        result,
        playedAt,
        30,
        Instant.now(),
        "1. e4 e5 *");
  }
}
