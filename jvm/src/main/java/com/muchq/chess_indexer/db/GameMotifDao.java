package com.muchq.chess_indexer.db;

import jakarta.inject.Singleton;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.SQLException;
import java.util.List;
import java.util.UUID;
import javax.sql.DataSource;

@Singleton
public class GameMotifDao {

  private final DataSource dataSource;

  public GameMotifDao(DataSource dataSource) {
    this.dataSource = dataSource;
  }

  public void replaceMotifs(UUID gameId, List<MotifRecord> motifs) {
    try (Connection connection = dataSource.getConnection()) {
      try (PreparedStatement deleteStmt = connection.prepareStatement(
          "DELETE FROM game_motifs WHERE game_id = ?")) {
        deleteStmt.setObject(1, gameId);
        deleteStmt.executeUpdate();
      }

      try (PreparedStatement insertStmt = connection.prepareStatement(
          "INSERT INTO game_motifs (game_id, motif_name, first_ply) VALUES (?, ?, ?)")) {
        for (MotifRecord motif : motifs) {
          insertStmt.setObject(1, gameId);
          insertStmt.setString(2, motif.name());
          insertStmt.setInt(3, motif.firstPly());
          insertStmt.addBatch();
        }
        insertStmt.executeBatch();
      }
    } catch (SQLException e) {
      throw new RuntimeException("Failed to update motifs", e);
    }
  }

  public record MotifRecord(String name, int firstPly) {}
}
