package com.muchq.chess_indexer.db;

import com.muchq.chess_indexer.model.GameRecord;
import com.muchq.chess_indexer.model.GameSummary;
import jakarta.inject.Singleton;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.time.Instant;
import java.time.ZoneOffset;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import javax.sql.DataSource;

@Singleton
public class GameDao {

  private final DataSource dataSource;

  public GameDao(DataSource dataSource) {
    this.dataSource = dataSource;
  }

  public UUID upsert(GameRecord record) {
    try (Connection connection = dataSource.getConnection();
         PreparedStatement statement = connection.prepareStatement("""
             INSERT INTO games (
               id, platform, game_uuid, end_time, rated, time_class, rules, eco,
               white_username, white_elo, black_username, black_elo, result, pgn, created_at
             ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
             ON CONFLICT (platform, game_uuid)
             DO UPDATE SET
               end_time = EXCLUDED.end_time,
               rated = EXCLUDED.rated,
               time_class = EXCLUDED.time_class,
               rules = EXCLUDED.rules,
               eco = EXCLUDED.eco,
               white_username = EXCLUDED.white_username,
               white_elo = EXCLUDED.white_elo,
               black_username = EXCLUDED.black_username,
               black_elo = EXCLUDED.black_elo,
               result = EXCLUDED.result,
               pgn = EXCLUDED.pgn
             RETURNING id
             """)) {
      UUID id = record.id() != null ? record.id() : UUID.randomUUID();
      statement.setObject(1, id);
      statement.setString(2, record.platform());
      statement.setString(3, record.gameUuid());
      statement.setObject(4, record.endTime().atOffset(ZoneOffset.UTC));
      statement.setBoolean(5, record.rated());
      statement.setString(6, record.timeClass());
      statement.setString(7, record.rules());
      statement.setString(8, record.eco());
      statement.setString(9, record.whiteUsername());
      statement.setInt(10, record.whiteElo());
      statement.setString(11, record.blackUsername());
      statement.setInt(12, record.blackElo());
      statement.setString(13, record.result());
      statement.setString(14, record.pgn());
      statement.setObject(15, Instant.now().atOffset(ZoneOffset.UTC));
      try (ResultSet rs = statement.executeQuery()) {
        if (rs.next()) {
          return rs.getObject(1, UUID.class);
        }
      }
      throw new RuntimeException("Failed to upsert game record");
    } catch (SQLException e) {
      throw new RuntimeException("Failed to upsert game", e);
    }
  }

  public void linkToRequest(UUID requestId, UUID gameId) {
    try (Connection connection = dataSource.getConnection();
         PreparedStatement statement = connection.prepareStatement("""
             INSERT INTO index_request_games (index_request_id, game_id)
             VALUES (?, ?)
             ON CONFLICT DO NOTHING
             """)) {
      statement.setObject(1, requestId);
      statement.setObject(2, gameId);
      statement.executeUpdate();
    } catch (SQLException e) {
      throw new RuntimeException("Failed to link request to game", e);
    }
  }

  public List<GameSummary> query(String sql, List<Object> params) {
    try (Connection connection = dataSource.getConnection();
         PreparedStatement statement = connection.prepareStatement(sql)) {
      for (int i = 0; i < params.size(); i++) {
        statement.setObject(i + 1, params.get(i));
      }

      try (ResultSet rs = statement.executeQuery()) {
        List<GameSummary> results = new ArrayList<>();
        while (rs.next()) {
          results.add(mapSummary(rs));
        }
        return results;
      }
    } catch (SQLException e) {
      throw new RuntimeException("Failed to query games", e);
    }
  }

  private GameSummary mapSummary(ResultSet rs) throws SQLException {
    return new GameSummary(
        rs.getObject("id", UUID.class),
        rs.getString("platform"),
        rs.getString("game_uuid"),
        rs.getObject("end_time", java.time.OffsetDateTime.class).toInstant(),
        rs.getString("white_username"),
        rs.getInt("white_elo"),
        rs.getString("black_username"),
        rs.getInt("black_elo"),
        rs.getString("result"),
        rs.getString("time_class"),
        rs.getString("eco"),
        rs.getBoolean("rated")
    );
  }
}
