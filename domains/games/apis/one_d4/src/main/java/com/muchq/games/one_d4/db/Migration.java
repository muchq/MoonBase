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
          indexed_at    TIMESTAMP NOT NULL DEFAULT current_timestamp(),
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
          indexed_at    TIMESTAMP NOT NULL DEFAULT now(),
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

  private static final String ADD_INDEXED_AT_COLUMN =
      "ALTER TABLE game_features ADD COLUMN IF NOT EXISTS indexed_at TIMESTAMP NOT NULL DEFAULT"
          + " now()";
  private static final String DROP_MOTIFS_JSON_COLUMN_H2 =
      "ALTER TABLE game_features DROP COLUMN IF EXISTS motifs_json";
  private static final String DROP_MOTIFS_JSON_COLUMN_PG =
      "ALTER TABLE game_features DROP COLUMN IF EXISTS motifs_json";

  // Drop has_* boolean motif columns — queries now use motif_occurrences directly.
  // Issued one per statement because H2 doesn't support comma-separated multi-column drops.
  private static final String[] DROP_HAS_MOTIF_COLUMNS = {
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_pin",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_cross_pin",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_fork",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_skewer",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_discovered_attack",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_discovered_mate",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_discovered_check",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_check",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_checkmate",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_promotion",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_promotion_with_check",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_promotion_with_checkmate",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_back_rank_mate",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_smothered_mate",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_sacrifice",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_zugzwang",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_double_check",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_interference",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_overloaded_piece",
  };

  // motif_occurrences: one row per motif firing per game. Dialect-neutral (UUID stored as string).
  private static final String CREATE_MOTIF_OCCURRENCES =
      """
      CREATE TABLE IF NOT EXISTS motif_occurrences (
          id           VARCHAR(36) NOT NULL PRIMARY KEY,
          game_url     VARCHAR(1024) NOT NULL REFERENCES game_features(game_url) ON DELETE CASCADE,
          motif        VARCHAR(50) NOT NULL,
          ply          INT NOT NULL,
          side         VARCHAR(5) NOT NULL,
          move_number  INT NOT NULL,
          description  TEXT,
          moved_piece  VARCHAR(20),
          attacker     VARCHAR(20),
          target       VARCHAR(20),
          is_discovered BOOLEAN NOT NULL DEFAULT FALSE,
          is_mate       BOOLEAN NOT NULL DEFAULT FALSE
      )
      """;
  private static final String CREATE_IDX_MOTIF_OCC_GAME_URL =
      "CREATE INDEX IF NOT EXISTS idx_motif_occ_game_url ON motif_occurrences(game_url)";
  private static final String CREATE_IDX_MOTIF_OCC_MOTIF =
      "CREATE INDEX IF NOT EXISTS idx_motif_occ_motif ON motif_occurrences(motif)";
  private static final String CREATE_IDX_MOTIF_OCC_PLY =
      "CREATE INDEX IF NOT EXISTS idx_motif_occ_ply ON motif_occurrences(game_url, ply)";

  // Structured fields for discovered attack/check occurrences (legacy ALTER TABLE)
  private static final String ADD_OCC_MOVED_PIECE =
      "ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS moved_piece VARCHAR(20)";
  private static final String ADD_OCC_ATTACKER =
      "ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS attacker VARCHAR(20)";
  private static final String ADD_OCC_TARGET =
      "ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS target VARCHAR(20)";
  private static final String ADD_OCC_IS_DISCOVERED =
      "ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS is_discovered BOOLEAN NOT NULL"
          + " DEFAULT FALSE";
  private static final String ADD_OCC_IS_MATE =
      "ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS is_mate BOOLEAN NOT NULL DEFAULT"
          + " FALSE";
  private static final String ADD_OCC_PIN_TYPE =
      "ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS pin_type VARCHAR(8)";

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

      stmt.execute(ADD_INDEXED_AT_COLUMN);

      // Drop legacy motifs_json column (replaced by motif_occurrences table)
      if (useH2) {
        stmt.execute(DROP_MOTIFS_JSON_COLUMN_H2);
      } else {
        stmt.execute(DROP_MOTIFS_JSON_COLUMN_PG);
      }

      stmt.execute(CREATE_MOTIF_OCCURRENCES);
      stmt.execute(CREATE_IDX_MOTIF_OCC_GAME_URL);
      stmt.execute(CREATE_IDX_MOTIF_OCC_MOTIF);
      stmt.execute(CREATE_IDX_MOTIF_OCC_PLY);

      // Structured fields on motif_occurrences
      stmt.execute(ADD_OCC_MOVED_PIECE);
      stmt.execute(ADD_OCC_ATTACKER);
      stmt.execute(ADD_OCC_TARGET);
      stmt.execute(ADD_OCC_IS_DISCOVERED);
      stmt.execute(ADD_OCC_IS_MATE);

      // Pin type for motif_occurrences
      stmt.execute(ADD_OCC_PIN_TYPE);

      // Drop has_* boolean motif columns — queries now target motif_occurrences directly.
      for (String drop : DROP_HAS_MOTIF_COLUMNS) {
        stmt.execute(drop);
      }

      LOG.info("Database migration completed successfully (H2={})", useH2);
    } catch (SQLException e) {
      throw new RuntimeException("Failed to run database migration", e);
    }
  }
}
