package com.muchq.games.one_d4.db;

import java.sql.SQLException;
import org.jdbi.v3.core.Handle;
import org.jdbi.v3.core.HandleCallback;
import org.jdbi.v3.core.Jdbi;
import org.jdbi.v3.core.statement.SqlStatements;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Execution bounds for the statements run by unattended loops, and the one entry point that applies
 * them. HikariCP's connectionTimeout bounds pool <em>checkout</em> only — nothing bounded
 * execution, so a statement wedged on a lock wait ran forever, and the loops that never re-enter
 * while a run is in flight (the first-page warmer's {@code @Scheduled(fixedDelay)}, the dispatch
 * poller's single thread, the hourly retention sweep) stayed wedged until a restart.
 *
 * <p>JDBC's queryTimeout cancels server-side execution (lock waits, slow plans). A true network
 * black hole additionally needs a driver-level socket timeout, which {@link DataSourceFactory}
 * defaults for Postgres URLs — see the constant there for how the two relate.
 */
public final class StatementTimeouts {
  private static final Logger LOG = LoggerFactory.getLogger(StatementTimeouts.class);

  /**
   * The bound for serving reads (GameFeatureDao's query/aggregate/occurrence paths) and the
   * dispatch poller's candidate scan. Ten seconds is far above any legitimate run of these —
   * LIMIT-bounded pages and an 8-row poll over tables with 7-day retention.
   *
   * <p>For the first-page warmer, the binding constraint is FirstPageCache.MAX_AGE (60s), not the
   * warmer's 30s tick — fixedDelay measures from completion, so ticks cannot overlap at any timeout
   * — and a warmer tick runs <em>two</em> bounded reads (query, then queryOccurrences), so its
   * worst successful case is 2x this value plus the 30s delay, which must stay under MAX_AGE or the
   * snapshot expires between refreshes. FirstPageWarmerTest pins that arithmetic. For the dispatch
   * poller (IndexingRequestDao.claimNext, every ≤5s on a single dedicated thread), a bounded
   * failure delays the next poll; an unbounded one used to stop the instance claiming work until a
   * restart.
   */
  public static final int SERVING_READ_SECONDS = 10;

  /**
   * The bound for the hourly retention tick's statements — the reclaim transaction and the three
   * deletes. Sized for a sweep, not a page: the steady-state delete covers roughly one hour of
   * aged-out rows, and even a post-outage backlog of days clears in well under this. Per statement,
   * not per tick — a tick where everything wedges can take up to four windows (~8 minutes) before
   * the hourly schedule re-arms, still bounded. The sweep is idempotent and re-runs in an hour, so
   * a truncated pass loses nothing — unlike the index worker's flushes, which have their own lease
   * and interrupt machinery and stay unbounded (GameFeatureDao.fetchForReanalysis likewise, as the
   * read half of the admin reanalysis batch loop).
   *
   * <p>Must stay under {@link DataSourceFactory}'s default Postgres socketTimeout, or the driver
   * would sever the connection under a legitimately long sweep before the server-side cancel fires.
   * DataSourceFactoryTest pins that ordering.
   */
  public static final int RETENTION_SWEEP_SECONDS = 120;

  private StatementTimeouts() {}

  /**
   * Runs {@code body} with every statement it opens carrying {@code timeoutSeconds}, set once on
   * the handle's statement config, and the session cleared on the way out. The bound being part of
   * the entry point — rather than a call each statement must remember — is what makes "this path is
   * bounded" structural: a new statement either goes through here and is bounded, or visibly
   * doesn't.
   */
  public static <T> T withStatementTimeout(
      Jdbi jdbi, int timeoutSeconds, HandleCallback<T, RuntimeException> body) {
    return jdbi.withHandle(
        h -> {
          h.getConfig(SqlStatements.class).setQueryTimeout(timeoutSeconds);
          try {
            return body.withHandle(h);
          } finally {
            clearSessionQueryTimeout(h);
          }
        });
  }

  /**
   * {@link #withStatementTimeout}'s transactional sibling, for bounded work that must stay one
   * transaction — the retention tick's reclaim, whose three UPDATEs settle abandoned requests as a
   * unit. Same bound-and-clear contract; the session clear runs inside the transaction, which is
   * safe because session settings are not transactional on either engine.
   */
  public static <T> T inTransactionWithTimeout(
      Jdbi jdbi, int timeoutSeconds, HandleCallback<T, RuntimeException> body) {
    return jdbi.inTransaction(
        h -> {
          h.getConfig(SqlStatements.class).setQueryTimeout(timeoutSeconds);
          try {
            return body.withHandle(h);
          } finally {
            clearSessionQueryTimeout(h);
          }
        });
  }

  /**
   * H2 scopes {@code Statement.setQueryTimeout} to the connection's session — {@code JdbcStatement}
   * delegates straight to {@code JdbcConnection.setQueryTimeout} — and HikariCP does not reset it
   * on checkin, so without this a single bounded statement would leave its timeout on the pooled
   * connection for every later statement, including the writes this class deliberately leaves
   * unbounded. Postgres scopes the timeout to the statement, so there the reset is a pure
   * client-side no-op (creating and closing a never-executed statement touches no network) — which
   * is why it runs unconditionally rather than carrying dialect knowledge.
   *
   * <p>Reset through the JDBC API rather than {@code SET QUERY_TIMEOUT 0}: H2 also caches the value
   * client-side in the connection, and raw SQL resets only the server session, leaving the cached
   * value to keep answering {@code getQueryTimeout} — and to keep applying — for later statements.
   */
  private static void clearSessionQueryTimeout(Handle h) {
    try (var stmt = h.getConnection().createStatement()) {
      stmt.setQueryTimeout(0);
    } catch (SQLException e) {
      // Log-and-swallow: this runs in a finally, where a throw would replace the statement's real
      // exception — including the timeout this bound exists to surface — and could fail an
      // otherwise-successful call. A connection too broken to accept the reset is one HikariCP
      // evicts, so the leak this guards against cannot outlive it.
      LOG.warn("Failed to clear session query timeout", e);
    }
  }
}
