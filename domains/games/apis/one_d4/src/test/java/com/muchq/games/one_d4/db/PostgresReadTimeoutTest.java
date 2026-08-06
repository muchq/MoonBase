package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.one_d4.api.dto.GameFeature;
import java.io.Closeable;
import java.net.URI;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.Statement;
import java.time.Clock;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import javax.sql.DataSource;
import org.jdbi.v3.core.Jdbi;
import org.jdbi.v3.core.statement.UnableToExecuteStatementException;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * The read-path query timeout, on the deployment dialect. H2's cooperative enforcement is covered
 * in {@link GameFeatureDaoTest}; Postgres enforces it differently — pgjdbc runs a cancel timer that
 * interrupts the server out-of-band — and Postgres is also the dialect where the DAO's H2-only
 * session cleanup must correctly do nothing: pgjdbc scopes the timeout to the statement, which this
 * suite pins rather than asserts in prose.
 *
 * <p>Runs against the real postgres that CI's build-and-test job provides via {@code
 * PG_TEST_DB_URL}; skips when that is unset. Uses a dedicated schema so it cannot collide with the
 * other suites sharing that scratch database.
 */
public class PostgresReadTimeoutTest {

  private static final String DB_URL_ENV = "PG_TEST_DB_URL";
  private static final String SCHEMA = "one_d4_pg_read_timeout_test";

  private DataSource dataSource;
  private UUID requestId;

  @BeforeEach
  public void setUp() throws Exception {
    String rawUrl = System.getenv(DB_URL_ENV);
    assumeTrue(
        rawUrl != null && !rawUrl.isBlank(),
        DB_URL_ENV + " is not set; skipping the real-postgres read-timeout suite");

    try (Connection conn = DriverManager.getConnection(jdbcUrl(rawUrl, null));
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP SCHEMA IF EXISTS " + SCHEMA + " CASCADE");
      stmt.execute("CREATE SCHEMA " + SCHEMA);
    }

    dataSource = DataSourceFactory.create(jdbcUrl(rawUrl, SCHEMA));
    new Migration(dataSource, false).run();

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
   * pgjdbc must cancel a running query at the timeout — server-side, out-of-band, mid-sleep. The
   * positive twin shares the fixture (same query shape, zero sleep), so a broken query cannot
   * masquerade as the timeout firing.
   */
  @Test
  public void slowQueryIsCancelledAtTheTimeoutOnPostgres() {
    GameFeatureDao bounded = new GameFeatureDao(Jdbi.create(dataSource), false, Clock.systemUTC());
    GameFeatureDao oneSecond =
        new GameFeatureDao(Jdbi.create(dataSource), false, Clock.systemUTC(), 1);
    insertOneGame("https://chess.com/game/pg-slow");

    // pg_sleep is volatile, so it runs per candidate row; void casts to the empty string.
    CompiledQuery slow =
        new CompiledQuery(
            "SELECT g.* FROM game_features g WHERE pg_sleep(5)::text = ''"
                + " ORDER BY g.played_at DESC, g.game_url ASC",
            List.of());

    long start = System.nanoTime();
    assertThatThrownBy(() -> oneSecond.query(slow, 10, 0))
        .as("a query outrunning the timeout must be cancelled, not awaited")
        .isInstanceOf(UnableToExecuteStatementException.class);
    long elapsedMillis = (System.nanoTime() - start) / 1_000_000;
    assertThat(elapsedMillis)
        .as("cancellation must arrive near the 1s timeout, not after the 5s sleep")
        .isLessThan(4_000);

    CompiledQuery fast =
        new CompiledQuery(
            "SELECT g.* FROM game_features g WHERE pg_sleep(0)::text = ''"
                + " ORDER BY g.played_at DESC, g.game_url ASC",
            List.of());
    assertThat(bounded.query(fast, 10, 0)).hasSize(1);
  }

  /**
   * The claim behind gating the session cleanup on H2 only: on Postgres a read's timeout must not
   * bound later statements on the same pooled connection, even though the DAO does nothing to clear
   * it there.
   *
   * <p>Pinned behaviorally, not by reading a client-side field: a fresh {@code PgStatement}'s
   * {@code getQueryTimeout()} is 0 by construction, so asserting it proves nothing. Instead, after
   * a 1s-bounded read, an <em>unbounded</em> 2s {@code pg_sleep} on the same pool must complete —
   * any leak, through the client timer or a server-side {@code statement_timeout}, would cancel it
   * at 1s. The {@code SHOW statement_timeout} probe additionally pins that pgjdbc's mechanism is
   * the client cancel timer and left no server-side session setting behind.
   */
  @Test
  public void readTimeoutDoesNotLeakAcrossStatementsOnPostgres() throws Exception {
    GameFeatureDao oneSecond =
        new GameFeatureDao(Jdbi.create(dataSource), false, Clock.systemUTC(), 1);
    insertOneGame("https://chess.com/game/pg-leak-probe");

    oneSecond.query(
        new CompiledQuery(
            "SELECT g.* FROM game_features g ORDER BY g.played_at DESC, g.game_url ASC", List.of()),
        10,
        0);

    // The pool is small and access is sequential, so this draws the connection the read used.
    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement()) {
      try (var rs = stmt.executeQuery("SHOW statement_timeout")) {
        assertThat(rs.next()).isTrue();
        assertThat(rs.getString(1))
            .as("the read bound must not become a server-side session setting")
            .isEqualTo("0");
      }
      try (var rs = stmt.executeQuery("SELECT pg_sleep(2)")) {
        assertThat(rs.next())
            .as("an unbounded statement after a bounded read must run to completion")
            .isTrue();
      }
    }
  }

  private void insertOneGame(String gameUrl) {
    GameFeatureDao dao = new GameFeatureDao(Jdbi.create(dataSource), false);
    dao.insertBatch(
        List.of(
            new GameFeature(
                UUID.randomUUID(),
                requestId,
                gameUrl,
                "CHESS_COM",
                "white",
                "black",
                2000,
                1900,
                null,
                "GM",
                "blitz",
                "B90",
                "Sicilian Defense Najdorf Variation",
                "Sicilian Defense",
                "1-0",
                Instant.now(),
                30,
                Instant.now(),
                "pgn")));
  }

  /**
   * Converts the libpq-style URL CI exports ({@code postgresql://user:pass@host:port/db}) into a
   * pgjdbc URL, same as the other PG-gated suites. pgjdbc does not accept credentials in the
   * authority, so they move to query params.
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
        + "?"
        + String.join("&", params);
  }

  private static String encode(String value) {
    return URLEncoder.encode(value, StandardCharsets.UTF_8);
  }
}
