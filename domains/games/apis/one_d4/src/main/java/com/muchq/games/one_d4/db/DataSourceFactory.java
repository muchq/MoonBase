package com.muchq.games.one_d4.db;

import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;
import javax.sql.DataSource;

public class DataSourceFactory {
  private DataSourceFactory() {
    throw new RuntimeException();
  }

  public static DataSource create(String jdbcUrl) {
    HikariConfig config = new HikariConfig();
    config.setJdbcUrl(jdbcUrl);
    config.setMaximumPoolSize(5);
    config.setMinimumIdle(1);
    config.setKeepaliveTime(60_000);
    config.setConnectionTestQuery("SELECT 1");
    config.setConnectionTimeout(10_000);
    return new HikariDataSource(config);
  }
}
