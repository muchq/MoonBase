package com.muchq.games.one_d4.db;

import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;
import java.util.OptionalInt;
import javax.sql.DataSource;
import org.jspecify.annotations.Nullable;

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
    return create(jdbcUrl, null, null);
  }

  /**
   * Credentials passed beside the URL rather than inside it, because pgjdbc URL-decodes query
   * parameter values. Interpolating a password into {@code ?password=...} silently corrupts any
   * secret containing {@code +} (becomes a space), truncates at {@code &}, rewrites {@code %41} to
   * {@code A}, and fails to parse at all on a stray {@code %} — reported as "No suitable driver",
   * which implicates nothing. {@code openssl rand -base64} emits {@code +} routinely, so this is a
   * live hazard for a password that already works elsewhere, not a theoretical one.
   *
   * <p>Hikari hands these to the driver as connection properties, which are not decoded, so the
   * secret's alphabet stops being a constraint. Embedding credentials in the URL's userinfo instead
   * would dodge the decoding but lose Hikari's password masking, whose regex only recognises the
   * {@code password=} query form — the URL would then appear complete in a connection-failure
   * message.
   *
   * <p>Null or empty leaves the config alone, so a URL that carries its own credentials (a Neon
   * connection string, and every H2 test URL) behaves exactly as before.
   */
  public static DataSource create(
      String jdbcUrl, @Nullable String username, @Nullable String password) {
    return new HikariDataSource(hikariConfig(jdbcUrl, username, password));
  }

  /**
   * Package-private so DataSourceFactoryTest can assert what {@link #create} actually wires —
   * probing the built config rather than {@link #defaultSocketTimeout} alone, which would stay
   * green if create() stopped applying it — without starting a pool against a database that isn't
   * there.
   */
  static HikariConfig hikariConfig(String jdbcUrl) {
    return hikariConfig(jdbcUrl, null, null);
  }

  static HikariConfig hikariConfig(
      String jdbcUrl, @Nullable String username, @Nullable String password) {
    HikariConfig config = new HikariConfig();
    config.setJdbcUrl(jdbcUrl);
    if (username != null && !username.isEmpty()) {
      config.setUsername(username);
    }
    // isEmpty, not isBlank: an all-whitespace password is legal, and an unset environment
    // variable arrives as "" rather than as whitespace.
    if (password != null && !password.isEmpty()) {
      config.setPassword(password);
    }
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
   * operator's explicit {@code socketTimeout} in {@code INDEXER_DB_URL} is never overridden.
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
