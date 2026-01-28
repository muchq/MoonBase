package com.muchq.chess_indexer.db;

import com.muchq.chess_indexer.model.IndexRequest;
import com.muchq.chess_indexer.model.IndexRequestStatus;
import jakarta.inject.Singleton;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.time.Instant;
import java.time.LocalDate;
import java.time.ZoneOffset;
import java.util.Optional;
import java.util.UUID;
import javax.sql.DataSource;

@Singleton
public class IndexRequestDao {

  private final DataSource dataSource;

  public IndexRequestDao(DataSource dataSource) {
    this.dataSource = dataSource;
  }

  public IndexRequest create(String platform, String username, LocalDate startDate, LocalDate endDate) {
    UUID id = UUID.randomUUID();
    Instant now = Instant.now();
    IndexRequestStatus status = IndexRequestStatus.PENDING;

    try (Connection connection = dataSource.getConnection();
         PreparedStatement statement = connection.prepareStatement("""
             INSERT INTO index_requests (
               id, platform, username, start_date, end_date, status, games_indexed, error_message, created_at, updated_at
             ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
             """)) {
      statement.setObject(1, id);
      statement.setString(2, platform);
      statement.setString(3, username);
      statement.setObject(4, startDate);
      statement.setObject(5, endDate);
      statement.setString(6, status.name());
      statement.setInt(7, 0);
      statement.setString(8, null);
      statement.setObject(9, now.atOffset(ZoneOffset.UTC));
      statement.setObject(10, now.atOffset(ZoneOffset.UTC));
      statement.executeUpdate();
    } catch (SQLException e) {
      throw new RuntimeException("Failed to create index request", e);
    }

    return new IndexRequest(id, platform, username, startDate, endDate, status, 0, null, now, now);
  }

  public Optional<IndexRequest> findById(UUID id) {
    try (Connection connection = dataSource.getConnection();
         PreparedStatement statement = connection.prepareStatement("""
             SELECT id, platform, username, start_date, end_date, status, games_indexed, error_message, created_at, updated_at
             FROM index_requests
             WHERE id = ?
             """)) {
      statement.setObject(1, id);
      try (ResultSet rs = statement.executeQuery()) {
        if (!rs.next()) {
          return Optional.empty();
        }
        return Optional.of(mapRow(rs));
      }
    } catch (SQLException e) {
      throw new RuntimeException("Failed to fetch index request", e);
    }
  }

  public void updateStatus(UUID id, IndexRequestStatus status, int gamesIndexed, String errorMessage) {
    Instant now = Instant.now();
    try (Connection connection = dataSource.getConnection();
         PreparedStatement statement = connection.prepareStatement("""
             UPDATE index_requests
             SET status = ?, games_indexed = ?, error_message = ?, updated_at = ?
             WHERE id = ?
             """)) {
      statement.setString(1, status.name());
      statement.setInt(2, gamesIndexed);
      statement.setString(3, errorMessage);
      statement.setObject(4, now.atOffset(ZoneOffset.UTC));
      statement.setObject(5, id);
      statement.executeUpdate();
    } catch (SQLException e) {
      throw new RuntimeException("Failed to update index request status", e);
    }
  }

  private IndexRequest mapRow(ResultSet rs) throws SQLException {
    UUID id = rs.getObject("id", UUID.class);
    String platform = rs.getString("platform");
    String username = rs.getString("username");
    LocalDate startDate = rs.getObject("start_date", LocalDate.class);
    LocalDate endDate = rs.getObject("end_date", LocalDate.class);
    IndexRequestStatus status = IndexRequestStatus.valueOf(rs.getString("status"));
    int gamesIndexed = rs.getInt("games_indexed");
    String errorMessage = rs.getString("error_message");
    Instant createdAt = rs.getObject("created_at", java.time.OffsetDateTime.class).toInstant();
    Instant updatedAt = rs.getObject("updated_at", java.time.OffsetDateTime.class).toInstant();
    return new IndexRequest(id, platform, username, startDate, endDate, status, gamesIndexed, errorMessage, createdAt, updatedAt);
  }
}
