package com.muchq.games.one_d4.db;

import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import javax.sql.DataSource;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class Migration {
  private static final Logger LOG = LoggerFactory.getLogger(Migration.class);

  private static final String H2_INDEXING_REQUESTS =
      """
      CREATE TABLE IF NOT EXISTS indexing_requests (
          id            UUID DEFAULT random_uuid() PRIMARY KEY,
          player        VARCHAR(255) NOT NULL,
          platform      VARCHAR(50) NOT NULL,
          start_month   VARCHAR(7) NOT NULL,
          end_month     VARCHAR(7) NOT NULL,
          status        VARCHAR(20) NOT NULL DEFAULT 'PENDING',
          created_at    TIMESTAMP NOT NULL DEFAULT current_timestamp(),
          updated_at    TIMESTAMP NOT NULL DEFAULT current_timestamp(),
          error_message TEXT,
          games_indexed INT DEFAULT 0
      )
      """;

  private static final String H2_GAME_FEATURES =
      """
      CREATE TABLE IF NOT EXISTS game_features (
          id            UUID DEFAULT random_uuid() PRIMARY KEY,
          request_id    UUID NOT NULL REFERENCES indexing_requests(id),
          game_url      VARCHAR(1024) NOT NULL UNIQUE,
          platform      VARCHAR(50) NOT NULL,
          white_username VARCHAR(255),
          black_username VARCHAR(255),
          white_elo     INT,
          black_elo     INT,
          time_class    VARCHAR(50),
          eco           VARCHAR(10),
          result        VARCHAR(20),
          played_at     TIMESTAMP,
          num_moves     INT,
          has_pin       BOOLEAN DEFAULT FALSE,
          has_cross_pin BOOLEAN DEFAULT FALSE,
          has_fork      BOOLEAN DEFAULT FALSE,
          has_skewer    BOOLEAN DEFAULT FALSE,
          has_discovered_attack BOOLEAN DEFAULT FALSE,
          has_check     BOOLEAN DEFAULT FALSE,
          has_checkmate BOOLEAN DEFAULT FALSE,
          has_promotion BOOLEAN DEFAULT FALSE,
          has_promotion_with_check BOOLEAN DEFAULT FALSE,
          has_promotion_with_checkmate BOOLEAN DEFAULT FALSE,
          motifs_json   TEXT,
          pgn           TEXT
      )
      """;

  private static final String H2_INDEXED_PERIODS =
      """
      CREATE TABLE IF NOT EXISTS indexed_periods (
          id            UUID DEFAULT random_uuid() PRIMARY KEY,
          player        VARCHAR(255) NOT NULL,
          platform      VARCHAR(50) NOT NULL,
          year_month    VARCHAR(7) NOT NULL,
          fetched_at    TIMESTAMP NOT NULL,
          is_complete   BOOLEAN NOT NULL,
          games_count   INT NOT NULL,
          UNIQUE (player, platform, year_month)
      )
      """;

  private static final String PG_INDEXING_REQUESTS =
      """
      CREATE TABLE IF NOT EXISTS indexing_requests (
          id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
          player        VARCHAR(255) NOT NULL,
          platform      VARCHAR(50) NOT NULL,
          start_month   VARCHAR(7) NOT NULL,
          end_month     VARCHAR(7) NOT NULL,
          status        VARCHAR(20) NOT NULL DEFAULT 'PENDING',
          created_at    TIMESTAMP NOT NULL DEFAULT now(),
          updated_at    TIMESTAMP NOT NULL DEFAULT now(),
          error_message TEXT,
          games_indexed INT DEFAULT 0
      )
      """;

  private static final String PG_GAME_FEATURES =
      """
      CREATE TABLE IF NOT EXISTS game_features (
          id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
          request_id    UUID NOT NULL REFERENCES indexing_requests(id),
          game_url      VARCHAR(1024) NOT NULL UNIQUE,
          platform      VARCHAR(50) NOT NULL,
          white_username VARCHAR(255),
          black_username VARCHAR(255),
          white_elo     INT,
          black_elo     INT,
          time_class    VARCHAR(50),
          eco           VARCHAR(10),
          result        VARCHAR(20),
          played_at     TIMESTAMP,
          num_moves     INT,
          has_pin       BOOLEAN DEFAULT FALSE,
          has_cross_pin BOOLEAN DEFAULT FALSE,
          has_fork      BOOLEAN DEFAULT FALSE,
          has_skewer    BOOLEAN DEFAULT FALSE,
          has_discovered_attack BOOLEAN DEFAULT FALSE,
          has_check     BOOLEAN DEFAULT FALSE,
          has_checkmate BOOLEAN DEFAULT FALSE,
          has_promotion BOOLEAN DEFAULT FALSE,
          has_promotion_with_check BOOLEAN DEFAULT FALSE,
          has_promotion_with_checkmate BOOLEAN DEFAULT FALSE,
          motifs_json   JSONB,
          pgn           TEXT
      )
      """;

  private static final String PG_INDEXED_PERIODS =
      """
      CREATE TABLE IF NOT EXISTS indexed_periods (
          id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
          player        VARCHAR(255) NOT NULL,
          platform      VARCHAR(50) NOT NULL,
          year_month    VARCHAR(7) NOT NULL,
          fetched_at    TIMESTAMP NOT NULL,
          is_complete   BOOLEAN NOT NULL,
          games_count   INT NOT NULL,
          UNIQUE (player, platform, year_month)
      )
      """;

  private final DataSource dataSource;
  private final boolean useH2;

  public Migration(DataSource dataSource, boolean useH2) {
    this.dataSource = dataSource;
    this.useH2 = useH2;
  }

  public void run() {
    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement()) {

      if (useH2) {
        stmt.execute(H2_INDEXING_REQUESTS);
        stmt.execute(H2_GAME_FEATURES);
        stmt.execute(H2_INDEXED_PERIODS);
      } else {
        stmt.execute(PG_INDEXING_REQUESTS);
        stmt.execute(PG_GAME_FEATURES);
        stmt.execute(PG_INDEXED_PERIODS);
      }

      LOG.info("Database migration completed successfully (H2={})", useH2);
    } catch (SQLException e) {
      throw new RuntimeException("Failed to run database migration", e);
    }
  }
}
