package com.muchq.games.one_d4.e2e;

import com.muchq.games.one_d4.db.H2SqlDialect;
import com.muchq.games.one_d4.db.SqlDialect;
import io.micronaut.context.annotation.Factory;
import io.micronaut.context.annotation.Replaces;
import io.micronaut.context.annotation.Requires;
import jakarta.inject.Singleton;

/**
 * When a test boots the module against H2 ({@code indexer.db.url} contains {@code :h2:}), replace
 * the production {@link com.muchq.games.one_d4.db.PostgresSqlDialect}. {@code IndexerModule} always
 * wires Postgres; it never inspects the URL for H2.
 */
@Factory
public class H2TestSqlDialectFactory {

  @Singleton
  @Replaces(SqlDialect.class)
  @Requires(property = "indexer.db.url", pattern = ".*:h2:.*")
  public SqlDialect h2SqlDialect() {
    return H2SqlDialect.INSTANCE;
  }
}
