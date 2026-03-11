package com.muchq.games.one_d4.db;

import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;
import javax.sql.DataSource;

public class DataSourceFactory {
  private DataSourceFactory() {}

  /**
   * Creates a DataSource from a JDBC URL. For PostgreSQL (e.g. Neon), credentials should be
   * embedded in the URL as query parameters: {@code ?user=u&password=p&sslmode=require}. For H2, no
   * credentials are required.
   */
  public static DataSource create(String jdbcUrl) {
    HikariConfig config = new HikariConfig();
    config.setJdbcUrl(jdbcUrl);
    config.setMaximumPoolSize(10);
    config.setMinimumIdle(2);
    if (!jdbcUrl.contains(":h2:")) {
      // Keep Neon compute warm and handle cold-start latency.
      config.setKeepaliveTime(60_000);
      config.setConnectionTestQuery("SELECT 1");
      config.setConnectionTimeout(10_000);
    }
    return new HikariDataSource(config);
  }
}
