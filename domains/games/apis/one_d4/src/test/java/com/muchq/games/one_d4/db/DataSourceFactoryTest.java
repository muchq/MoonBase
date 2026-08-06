package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import org.junit.jupiter.api.Test;

public class DataSourceFactoryTest {

  @Test
  public void postgresUrlWithoutSocketTimeoutGetsTheDefault() {
    assertThat(DataSourceFactory.defaultSocketTimeout("jdbc:postgresql://host:5432/db?user=u"))
        .hasValue(150);
  }

  @Test
  public void explicitSocketTimeoutInTheUrlWins() {
    assertThat(
            DataSourceFactory.defaultSocketTimeout(
                "jdbc:postgresql://host:5432/db?user=u&socketTimeout=30"))
        .as("an operator's explicit choice in the URL must never be overridden")
        .isEmpty();
  }

  @Test
  public void h2UrlsGetNoSocketTimeout() {
    // H2 rejects unknown connection properties outright, so the default must be Postgres-only.
    assertThat(DataSourceFactory.defaultSocketTimeout("jdbc:h2:mem:x;DB_CLOSE_DELAY=-1")).isEmpty();
  }

  /**
   * A long-running DELETE sends nothing over the socket until it finishes, so the driver-level
   * socket timeout must exceed the retention sweep's statement bound — otherwise a healthy
   * connection is severed mid-sweep before the server-side cancel fires. This is the ordering the
   * two constants' javadocs promise each other.
   */
  @Test
  public void socketTimeoutExceedsTheLongestSilentStatementBound() {
    assertThat(DataSourceFactory.PG_SOCKET_TIMEOUT_SECONDS)
        .isGreaterThan(StatementTimeouts.RETENTION_SWEEP_SECONDS);
  }
}
