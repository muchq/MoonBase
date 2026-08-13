package com.muchq.games.one_d4.db;

import javax.sql.DataSource;
import org.jdbi.v3.core.Jdbi;

/**
 * Shared test helper for creating an in-memory H2 database with JDBI.
 *
 * <p>H2 is a {@code runtime_deps} of {@code :test_db} — the library that opens {@code
 * jdbc:h2:mem:…} — and of the module-boot e2e suites that set {@code indexer.db.url} without going
 * through this helper. It is not declared on every suite that happens to depend on {@code :db}.
 */
public final class TestDb {

  private final DataSource dataSource;
  private final Jdbi jdbi;

  private TestDb(DataSource dataSource, Jdbi jdbi) {
    this.dataSource = dataSource;
    this.jdbi = jdbi;
  }

  public static TestDb create(String name) {
    // nanoTime, not currentTimeMillis: two tests starting in the same millisecond would silently
    // share a database.
    String jdbcUrl = "jdbc:h2:mem:" + name + "_" + System.nanoTime() + ";DB_CLOSE_DELAY=-1";
    DataSource dataSource = DataSourceFactory.create(jdbcUrl);
    new Migration(dataSource, true).run();
    return new TestDb(dataSource, Jdbi.create(dataSource));
  }

  public DataSource dataSource() {
    return dataSource;
  }

  public Jdbi jdbi() {
    return jdbi;
  }
}
