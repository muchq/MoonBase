package com.muchq.games.one_d4.e2e;

import com.muchq.games.one_d4.db.H2SqlDialect;
import com.muchq.games.one_d4.db.PostgresSqlDialect;
import com.muchq.games.one_d4.db.SqlDialect;
import io.micronaut.context.annotation.Factory;
import io.micronaut.context.annotation.Replaces;
import jakarta.inject.Named;
import jakarta.inject.Singleton;

/**
 * Replaces {@code IndexerModule.sqlDialect} whenever e2e_support is on the classpath. Reads the
 * same {@code indexerJdbcUrl} bean the DataSource uses, so a context cannot get an H2 pool with a
 * Postgres dialect (or the reverse).
 */
@Factory
public class TestSqlDialectFactory {

  @Singleton
  @Replaces(SqlDialect.class)
  public SqlDialect sqlDialect(@Named("indexerJdbcUrl") String jdbcUrl) {
    return jdbcUrl.contains(":h2:") ? new H2SqlDialect() : new PostgresSqlDialect();
  }
}
