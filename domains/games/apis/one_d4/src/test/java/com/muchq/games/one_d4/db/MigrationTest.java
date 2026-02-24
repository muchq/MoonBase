package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import java.sql.Connection;
import java.sql.DatabaseMetaData;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.UUID;
import javax.sql.DataSource;
import org.junit.Before;
import org.junit.Test;

public class MigrationTest {

  private DataSource dataSource;

  @Before
  public void setUp() {
    String jdbcUrl = "jdbc:h2:mem:migration_" + System.currentTimeMillis() + ";DB_CLOSE_DELAY=-1";
    dataSource = DataSourceFactory.create(jdbcUrl, "sa", "");
  }

  @Test
  public void run_createsMotifOccurrencesTableAndHasDiscoveredCheckColumn() throws Exception {
    Migration migration = new Migration(dataSource, true);
    migration.run();

    try (Connection conn = dataSource.getConnection()) {
      DatabaseMetaData meta = conn.getMetaData();
      try (ResultSet tables =
          meta.getTables(null, null, "MOTIF_OCCURRENCES", new String[] {"TABLE"})) {
        assertThat(tables.next()).as("motif_occurrences table should exist").isTrue();
      }

      try (ResultSet columns =
          meta.getColumns(null, null, "GAME_FEATURES", "HAS_DISCOVERED_CHECK")) {
        assertThat(columns.next())
            .as("game_features.has_discovered_check column should exist")
            .isTrue();
      }
    }
  }

  @Test
  public void run_motifOccurrencesTableAcceptsInsertAndSelect() throws Exception {
    Migration migration = new Migration(dataSource, true);
    migration.run();

    UUID requestId = UUID.randomUUID();
    String gameUrl = "https://chess.com/game/migration-test";

    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement()) {
      stmt.execute(
          "INSERT INTO indexing_requests (id, player, platform, start_month, end_month, status)"
              + " VALUES ('"
              + requestId
              + "', 'p', 'CHESS_COM', '2024-01', '2024-01', 'COMPLETED')");
      stmt.execute(
          "INSERT INTO game_features (request_id, game_url, platform, num_moves, indexed_at)"
              + " VALUES ('"
              + requestId
              + "', '"
              + gameUrl
              + "', 'CHESS_COM', 10, now())");
      stmt.execute(
          "INSERT INTO motif_occurrences (id, game_url, motif, ply, side, move_number, description)"
              + " VALUES ('"
              + UUID.randomUUID()
              + "', '"
              + gameUrl
              + "', 'CHECK', 5, 'white', 3, 'Check at move 3')");
    }

    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement();
        ResultSet rs =
            stmt.executeQuery(
                "SELECT game_url, motif, move_number, description FROM motif_occurrences"
                    + " WHERE game_url = '"
                    + gameUrl
                    + "'")) {
      assertThat(rs.next()).isTrue();
      assertThat(rs.getString("game_url")).isEqualTo(gameUrl);
      assertThat(rs.getString("motif")).isEqualTo("CHECK");
      assertThat(rs.getInt("move_number")).isEqualTo(3);
      assertThat(rs.getString("description")).isEqualTo("Check at move 3");
      assertThat(rs.next()).isFalse();
    }
  }
}
