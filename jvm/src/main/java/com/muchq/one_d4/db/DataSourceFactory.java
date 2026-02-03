package com.muchq.one_d4.db;

import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;
import javax.sql.DataSource;

public class DataSourceFactory {
  private DataSourceFactory() {}

  public static DataSource create(String jdbcUrl, String username, String password) {
    HikariConfig config = new HikariConfig();
    config.setJdbcUrl(jdbcUrl);
    config.setUsername(username);
    config.setPassword(password);
    config.setMaximumPoolSize(10);
    config.setMinimumIdle(2);
    return new HikariDataSource(config);
  }
}
