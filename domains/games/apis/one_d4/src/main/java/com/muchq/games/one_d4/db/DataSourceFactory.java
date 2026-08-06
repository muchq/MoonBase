package com.muchq.games.one_d4.db;

import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;
import java.util.OptionalInt;
import javax.sql.DataSource;

public class DataSourceFactory {

  /**
   * Default driver-level socket timeout for Postgres, in seconds. The statement timeouts in {@link
   * StatementTimeouts} cancel server-side execution, but that cancellation travels over the network
   * — on a TCP black hole only a socket timeout gets the connection back, and pgjdbc's default is
   * to wait forever.
   *
   * <p>Must exceed the longest legitimately <em>silent</em> statement, which is the retention sweep
   * ({@link StatementTimeouts#RETENTION_SWEEP_SECONDS}): a long-running DELETE sends nothing until
   * it finishes, so a smaller socket timeout would sever a healthy connection mid-sweep before the
   * server-side bound fires. DataSourceFactoryTest pins that ordering. This is a default, not a
   * mandate — a {@code socketTimeout} already present in the JDBC URL wins.
   */
  static final int PG_SOCKET_TIMEOUT_SECONDS = 150;

  private DataSourceFactory() {
    throw new RuntimeException();
  }

  public static DataSource create(String jdbcUrl) {
    return new HikariDataSource(hikariConfig(jdbcUrl));
  }

  /**
   * Package-private so DataSourceFactoryTest can assert what {@link #create} actually wires —
   * probing the built config rather than {@link #defaultSocketTimeout} alone, which would stay
   * green if create() stopped applying it — without starting a pool against a database that isn't
   * there.
   */
  static HikariConfig hikariConfig(String jdbcUrl) {
    HikariConfig config = new HikariConfig();
    config.setJdbcUrl(jdbcUrl);
    config.setMaximumPoolSize(5);
    config.setMinimumIdle(1);
    config.setKeepaliveTime(60_000);
    config.setConnectionTestQuery("SELECT 1");
    config.setConnectionTimeout(10_000);
    defaultSocketTimeout(jdbcUrl)
        .ifPresent(
            seconds -> config.addDataSourceProperty("socketTimeout", String.valueOf(seconds)));
    return config;
  }

  /**
   * The socket-timeout default to apply for this URL, if any. Postgres only — H2 rejects unknown
   * connection properties outright — and only when the URL does not already carry one, so an
   * operator's explicit choice in {@code /etc/one_d4/db_config} is never overridden.
   */
  static OptionalInt defaultSocketTimeout(String jdbcUrl) {
    // Matched at a parameter boundary so the substring appearing inside another parameter's
    // value cannot silently suppress the default.
    if (!jdbcUrl.startsWith("jdbc:postgresql:")
        || jdbcUrl.contains("?socketTimeout=")
        || jdbcUrl.contains("&socketTimeout=")) {
      return OptionalInt.empty();
    }
    return OptionalInt.of(PG_SOCKET_TIMEOUT_SECONDS);
  }
}
