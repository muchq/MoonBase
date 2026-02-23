package com.muchq.games.one_d4.db;

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import javax.sql.DataSource;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class IndexingRequestDao implements IndexingRequestStore {
  private static final Logger LOG = LoggerFactory.getLogger(IndexingRequestDao.class);

  private final DataSource dataSource;

  public IndexingRequestDao(DataSource dataSource) {
    this.dataSource = dataSource;
  }

  @Override
  public UUID create(String player, String platform, String startMonth, String endMonth) {
    UUID id = UUID.randomUUID();
    String sql =
        """
        INSERT INTO indexing_requests (id, player, platform, start_month, end_month)
        VALUES (?, ?, ?, ?, ?)
        """;
    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps = conn.prepareStatement(sql)) {
      ps.setObject(1, id);
      ps.setString(2, player);
      ps.setString(3, platform);
      ps.setString(4, startMonth);
      ps.setString(5, endMonth);
      ps.executeUpdate();
      return id;
    } catch (SQLException e) {
      throw new RuntimeException("Failed to create indexing request", e);
    }
  }

  @Override
  public Optional<IndexingRequest> findById(UUID id) {
    String sql = "SELECT * FROM indexing_requests WHERE id = ?";
    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps = conn.prepareStatement(sql)) {
      ps.setObject(1, id);
      try (ResultSet rs = ps.executeQuery()) {
        if (rs.next()) {
          return Optional.of(mapRow(rs));
        }
        return Optional.empty();
      }
    } catch (SQLException e) {
      throw new RuntimeException("Failed to find indexing request", e);
    }
  }

  @Override
  public Optional<IndexingRequest> findExistingRequest(
      String player, String platform, String startMonth, String endMonth) {
    String sql =
        """
        SELECT * FROM indexing_requests
        WHERE player = ? AND platform = ? AND start_month = ? AND end_month = ?
          AND status IN ('PENDING', 'PROCESSING')
        ORDER BY created_at ASC
        LIMIT 1
        """;
    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps = conn.prepareStatement(sql)) {
      ps.setString(1, player);
      ps.setString(2, platform);
      ps.setString(3, startMonth);
      ps.setString(4, endMonth);
      try (ResultSet rs = ps.executeQuery()) {
        if (rs.next()) {
          return Optional.of(mapRow(rs));
        }
        return Optional.empty();
      }
    } catch (SQLException e) {
      throw new RuntimeException("Failed to find existing indexing request", e);
    }
  }

  @Override
  public List<IndexingRequest> listRecent(int limit) {
    String sql = "SELECT * FROM indexing_requests ORDER BY created_at DESC LIMIT ?";
    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps = conn.prepareStatement(sql)) {
      ps.setInt(1, limit);
      try (ResultSet rs = ps.executeQuery()) {
        List<IndexingRequest> results = new ArrayList<>();
        while (rs.next()) {
          results.add(mapRow(rs));
        }
        return results;
      }
    } catch (SQLException e) {
      throw new RuntimeException("Failed to list indexing requests", e);
    }
  }

  @Override
  public void updateStatus(UUID id, String status, String errorMessage, int gamesIndexed) {
    String sql =
        """
        UPDATE indexing_requests
        SET status = ?, error_message = ?, games_indexed = ?, updated_at = now()
        WHERE id = ?
        """;
    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps = conn.prepareStatement(sql)) {
      ps.setString(1, status);
      ps.setString(2, errorMessage);
      ps.setInt(3, gamesIndexed);
      ps.setObject(4, id);
      ps.executeUpdate();
    } catch (SQLException e) {
      throw new RuntimeException("Failed to update indexing request status", e);
    }
  }

  private IndexingRequest mapRow(ResultSet rs) throws SQLException {
    return new IndexingRequest(
        UUID.fromString(rs.getString("id")),
        rs.getString("player"),
        rs.getString("platform"),
        rs.getString("start_month"),
        rs.getString("end_month"),
        rs.getString("status"),
        rs.getTimestamp("created_at").toInstant(),
        rs.getTimestamp("updated_at").toInstant(),
        rs.getString("error_message"),
        rs.getInt("games_indexed"));
  }
}
