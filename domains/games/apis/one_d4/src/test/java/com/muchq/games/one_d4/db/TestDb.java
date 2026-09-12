package com.muchq.games.one_d4.db;

import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.Statement;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import javax.sql.DataSource;
import org.jdbi.v3.core.Jdbi;
import org.opentest4j.TestAbortedException;

/**
 * A migrated Postgres schema for one suite, from {@code PG_TEST_DB_URL}.
 *
 * <p>One engine, so the DDL has one spelling and the schema is designed for the database it is
 * deployed to (#1532).
 *
 * <p>Every suite gets its own schema off the one scratch database, because bazel runs them in
 * parallel and tables are shared mutable state otherwise. The drop is on the way in rather than the
 * way out: a suite that crashed should not leave the next run reading its rows, and a schema left
 * behind costs nothing.
 */
public final class TestDb {

  /**
   * The live pool per schema name, so a {@code @BeforeEach} that builds a TestDb per test leaves
   * one pool behind rather than one per test. It does not leave zero: nothing tells a JUnit test
   * when its DataSource is finished with, and no suite closes one. Unbounded, four test JVMs
   * running in parallel exhausted the scratch database's client slots — {@code FATAL: sorry, too
   * many clients already} — which reads as a broken database rather than as a leak.
   */
  private static final Map<String, HikariDataSource> POOLS = new ConcurrentHashMap<>();

  private final DataSource dataSource;
  private final Jdbi jdbi;
  private final String schema;
  private final String url;

  private TestDb(DataSource dataSource, Jdbi jdbi, String schema, String url) {
    this.dataSource = dataSource;
    this.jdbi = jdbi;
    this.schema = schema;
    this.url = url;
  }

  /**
   * A migrated schema named for the suite.
   *
   * @throws TestAbortedException when no database is configured and none is required, which JUnit
   *     reports as a skip; see {@link PgTestUrls#requireRawUrl}.
   */
  public static TestDb create(String name) {
    TestDb db = emptySchema(name);
    new Migration(db.dataSource).run();
    return db;
  }

  /**
   * An empty, unmigrated schema, for a suite whose subject is the migration itself. Same gating as
   * {@link #create}.
   */
  public static TestDb emptySchema(String name) {
    String rawUrl = PgTestUrls.requireRawUrl();
    String schema = freshSchema("one_d4_" + name);
    String url = PgTestUrls.jdbcUrl(rawUrl, schema);
    DataSource dataSource = pool(name, url);
    return new TestDb(dataSource, Jdbi.create(dataSource), schema, url);
  }

  /**
   * A migrated schema's pgjdbc URL, for a suite that hands one to {@code ApplicationContext} rather
   * than opening a DataSource itself. Same gating as {@link #create}.
   */
  public static String jdbcUrlFor(String name) {
    TestDb db = create(name);
    // The caller builds its own pool from the URL, so this one has no further use — and holding it
    // open would double this suite's share of the scratch database's connections.
    closePool(name);
    return db.url;
  }

  /**
   * Every production setting from {@link DataSourceFactory}, with the pool floor dropped to zero: a
   * suite that has finished querying should be holding nothing. Replacing the pool registered under
   * {@code name} closes it.
   */
  private static DataSource pool(String name, String url) {
    HikariConfig config = DataSourceFactory.hikariConfig(url);
    config.setMinimumIdle(0);
    HikariDataSource dataSource = new HikariDataSource(config);
    closePool(name);
    POOLS.put(name, dataSource);
    return dataSource;
  }

  private static void closePool(String name) {
    HikariDataSource previous = POOLS.remove(name);
    if (previous != null) {
      previous.close();
    }
  }

  /** Drops and recreates `schema`, returning it. */
  private static String freshSchema(String schema) {
    try (Connection conn =
            DriverManager.getConnection(PgTestUrls.jdbcUrl(PgTestUrls.requireRawUrl(), null));
        Statement stmt = conn.createStatement()) {
      // Identifiers cannot be bound, and these names are test-local literals.
      stmt.execute("DROP SCHEMA IF EXISTS " + schema + " CASCADE");
      stmt.execute("CREATE SCHEMA " + schema);
    } catch (java.sql.SQLException e) {
      throw new IllegalStateException("could not prepare the schema " + schema, e);
    }
    return schema;
  }

  public DataSource dataSource() {
    return dataSource;
  }

  public Jdbi jdbi() {
    return jdbi;
  }

  /** The schema this suite's tables live in, for a test that needs to name it. */
  public String schema() {
    return schema;
  }

  /** This schema's pgjdbc URL. */
  public String url() {
    return url;
  }
}
