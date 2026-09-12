package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.io.Closeable;
import java.sql.Connection;
import java.sql.Statement;
import javax.sql.DataSource;
import org.jdbi.v3.core.Jdbi;
import org.jdbi.v3.core.statement.UnableToExecuteStatementException;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * What the statement bound actually does to a running statement, and what it leaves behind. {@link
 * StatementTimeouts} unwinds nothing on the way out, which is only correct because pgjdbc enforces
 * the bound with a client-side cancel timer scoped to the statement — so that is pinned
 * behaviorally here rather than asserted in prose.
 *
 * <p>Its own suite because it runs no migrations and touches no tables: everything here is
 * expressible with {@code pg_sleep} and session probes.
 */
public class PostgresReadTimeoutTest {

  private DataSource dataSource;
  private Jdbi jdbi;

  @BeforeEach
  public void setUp() {
    String rawUrl = PgTestUrls.requireRawUrl();

    dataSource = DataSourceFactory.create(PgTestUrls.jdbcUrl(rawUrl, null));
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
   * A bounded statement's timeout must not bound later statements on the same pooled connection.
   * This is the claim that lets {@link StatementTimeouts} clear nothing afterwards.
   *
   * <p>Pinned behaviorally, not by reading a client-side field: a fresh {@code PgStatement}'s
   * {@code getQueryTimeout()} is 0 by construction, so asserting it proves nothing. Instead, after
   * a 1s-bounded statement, an <em>unbounded</em> 2s {@code pg_sleep} on the same pool must
   * complete — any leak, through the client timer or a server-side {@code statement_timeout}, would
   * cancel it at 1s. The {@code SHOW statement_timeout} probe additionally pins that the bound left
   * no server-side session setting behind.
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
}
