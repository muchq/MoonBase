package com.muchq.games.one_d4.db;

import javax.sql.DataSource;
import org.jdbi.v3.core.Jdbi;

/** Shared test helper for creating an in-memory H2 database with JDBI. */
public final class TestDb {

  private final DataSource dataSource;
  private final Jdbi jdbi;

  private TestDb(DataSource dataSource, Jdbi jdbi) {
    this.dataSource = dataSource;
    this.jdbi = jdbi;
  }

  public static TestDb create(String name) {
    String jdbcUrl =
        "jdbc:h2:mem:" + name + "_" + System.currentTimeMillis() + ";DB_CLOSE_DELAY=-1";
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
