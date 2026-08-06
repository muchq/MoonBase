package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

import java.io.Closeable;
import java.net.URI;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.sql.Connection;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import javax.sql.DataSource;
import org.jdbi.v3.core.Jdbi;
import org.jdbi.v3.core.statement.UnableToExecuteStatementException;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * The statement-timeout mechanism on the deployment dialect. H2's cooperative enforcement is
 * covered in {@link StatementTimeoutsTest}; Postgres enforces the bound differently — pgjdbc runs a
 * cancel timer that interrupts the server out-of-band — and Postgres is also the dialect where the
 * session cleanup must be a harmless no-op: pgjdbc scopes the timeout to the statement, which this
 * suite pins behaviorally rather than asserting in prose. (The cleanup runs unconditionally on both
 * dialects; on Postgres it is pure client-side bookkeeping.)
 *
 * <p>Runs against the real postgres that CI's build-and-test job provides via {@code
 * PG_TEST_DB_URL}; skips when that is unset. Runs no migrations and touches no tables — everything
 * here is expressible with {@code pg_sleep} and session probes.
 */
public class PostgresReadTimeoutTest {

  private static final String DB_URL_ENV = "PG_TEST_DB_URL";

  private DataSource dataSource;
  private Jdbi jdbi;

  @BeforeEach
  public void setUp() {
    String rawUrl = System.getenv(DB_URL_ENV);
    assumeTrue(
        rawUrl != null && !rawUrl.isBlank(),
        DB_URL_ENV + " is not set; skipping the real-postgres read-timeout suite");

    dataSource = DataSourceFactory.create(jdbcUrl(rawUrl));
    jdbi = Jdbi.create(dataSource);
  }

  @AfterEach
  public void tearDown() throws Exception {
    if (dataSource instanceof Closeable closeable) {
      closeable.close();
    }
  }

  /**
   * pgjdbc must cancel a running statement at the bound — server-side, out-of-band, mid-sleep. The
   * positive twin shares the query shape with zero sleep, so a broken query cannot masquerade as
   * the bound firing.
   */
  @Test
  public void slowExecutionIsCancelledAtTheBoundOnPostgres() {
    long start = System.nanoTime();
    assertThatThrownBy(
            () ->
                StatementTimeouts.withStatementTimeout(
                    jdbi,
                    1,
                    h -> h.createQuery("SELECT pg_sleep(5)::text").mapTo(String.class).one()))
        .as("a statement outrunning the bound must be cancelled, not awaited")
        .isInstanceOf(UnableToExecuteStatementException.class);
    long elapsedMillis = (System.nanoTime() - start) / 1_000_000;
    assertThat(elapsedMillis)
        .as("cancellation must arrive near the 1s bound, not after the 5s sleep")
        .isLessThan(4_000);

    String result =
        StatementTimeouts.withStatementTimeout(
            jdbi, 1, h -> h.createQuery("SELECT pg_sleep(0)::text").mapTo(String.class).one());
    assertThat(result).isEqualTo("");
  }

  /**
   * On Postgres a bounded statement's timeout must not bound later statements on the same pooled
   * connection, even though the cleanup does nothing driver-visible there.
   *
   * <p>Pinned behaviorally, not by reading a client-side field: a fresh {@code PgStatement}'s
   * {@code getQueryTimeout()} is 0 by construction, so asserting it proves nothing. Instead, after
   * a 1s-bounded statement, an <em>unbounded</em> 2s {@code pg_sleep} on the same pool must
   * complete — any leak, through the client timer or a server-side {@code statement_timeout}, would
   * cancel it at 1s. The {@code SHOW statement_timeout} probe additionally pins that pgjdbc's
   * mechanism is the client cancel timer and left no server-side session setting behind.
   */
  @Test
  public void boundDoesNotLeakAcrossStatementsOnPostgres() throws Exception {
    StatementTimeouts.withStatementTimeout(
        jdbi, 1, h -> h.createQuery("SELECT 1").mapTo(Integer.class).one());

    // The pool is small and access is sequential, so this draws the connection the read used.
    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement()) {
      try (var rs = stmt.executeQuery("SHOW statement_timeout")) {
        assertThat(rs.next()).isTrue();
        assertThat(rs.getString(1))
            .as("the bound must not become a server-side session setting")
            .isEqualTo("0");
      }
      try (var rs = stmt.executeQuery("SELECT pg_sleep(2)")) {
        assertThat(rs.next())
            .as("an unbounded statement after a bounded one must run to completion")
            .isTrue();
      }
    }
  }

  /**
   * The socket-timeout default, proven through the real driver rather than the built config: this
   * suite's pool came from {@code DataSourceFactory.create} on a URL without {@code socketTimeout},
   * and pgjdbc surfaces the applied value as {@code Connection.getNetworkTimeout} (milliseconds,
   * backed by the socket's SO_TIMEOUT).
   */
  @Test
  public void createAppliesTheSocketTimeoutDefaultToRealConnections() throws Exception {
    try (Connection conn = dataSource.getConnection()) {
      assertThat(conn.getNetworkTimeout())
          .as("the default must reach the actual socket")
          .isEqualTo(DataSourceFactory.PG_SOCKET_TIMEOUT_SECONDS * 1000);
    }
  }

  /**
   * Converts the libpq-style URL CI exports ({@code postgresql://user:pass@host:port/db}) into a
   * pgjdbc URL, same as the other PG-gated suites. pgjdbc does not accept credentials in the
   * authority, so they move to query params.
   */
  private static String jdbcUrl(String rawUrl) {
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
