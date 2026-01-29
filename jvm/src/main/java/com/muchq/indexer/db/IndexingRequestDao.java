package com.muchq.indexer.db;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.sql.DataSource;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.Optional;
import java.util.UUID;

public class IndexingRequestDao implements IndexingRequestStore {
    private static final Logger LOG = LoggerFactory.getLogger(IndexingRequestDao.class);

    private final DataSource dataSource;

    public IndexingRequestDao(DataSource dataSource) {
        this.dataSource = dataSource;
    }

    @Override
    public UUID create(String player, String platform, String startMonth, String endMonth) {
        String sql = """
            INSERT INTO indexing_requests (player, platform, start_month, end_month)
            VALUES (?, ?, ?, ?)
            RETURNING id
            """;
        try (Connection conn = dataSource.getConnection();
             PreparedStatement ps = conn.prepareStatement(sql)) {
            ps.setString(1, player);
            ps.setString(2, platform);
            ps.setString(3, startMonth);
            ps.setString(4, endMonth);
            try (ResultSet rs = ps.executeQuery()) {
                rs.next();
                return UUID.fromString(rs.getString("id"));
            }
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
    public void updateStatus(UUID id, String status, String errorMessage, int gamesIndexed) {
        String sql = """
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
                rs.getInt("games_indexed")
        );
    }
}
