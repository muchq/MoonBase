package com.muchq.chess_indexer.db;

import com.muchq.chess_indexer.model.GameFeatures;
import jakarta.inject.Singleton;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.SQLException;
import javax.sql.DataSource;

@Singleton
public class GameFeaturesDao {

  private final DataSource dataSource;

  public GameFeaturesDao(DataSource dataSource) {
    this.dataSource = dataSource;
  }

  public void upsert(GameFeatures features) {
    try (Connection connection = dataSource.getConnection();
         PreparedStatement statement = connection.prepareStatement("""
             INSERT INTO game_features (
               game_id, total_plies, has_castle, has_promotion, has_check, has_checkmate
             ) VALUES (?, ?, ?, ?, ?, ?)
             ON CONFLICT (game_id)
             DO UPDATE SET
               total_plies = EXCLUDED.total_plies,
               has_castle = EXCLUDED.has_castle,
               has_promotion = EXCLUDED.has_promotion,
               has_check = EXCLUDED.has_check,
               has_checkmate = EXCLUDED.has_checkmate
             """)) {
      statement.setObject(1, features.gameId());
      statement.setInt(2, features.totalPlies());
      statement.setBoolean(3, features.hasCastle());
      statement.setBoolean(4, features.hasPromotion());
      statement.setBoolean(5, features.hasCheck());
      statement.setBoolean(6, features.hasCheckmate());
      statement.executeUpdate();
    } catch (SQLException e) {
      throw new RuntimeException("Failed to upsert game features", e);
    }
  }
}
