package com.muchq.indexer.db;

import com.muchq.indexer.chessql.compiler.CompiledQuery;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.sql.DataSource;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Timestamp;
import java.sql.Types;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

public class GameFeatureDao {
    private static final Logger LOG = LoggerFactory.getLogger(GameFeatureDao.class);

    private final DataSource dataSource;

    public GameFeatureDao(DataSource dataSource) {
        this.dataSource = dataSource;
    }

    public void insert(GameFeatureRow row) {
        String sql = """
            INSERT INTO game_features (
                request_id, game_url, platform, white_username, black_username,
                white_elo, black_elo, time_class, eco, result, played_at, num_moves,
                has_pin, has_cross_pin, has_fork, has_skewer, has_discovered_attack,
                motifs_json, pgn
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?::jsonb, ?)
            ON CONFLICT (game_url) DO NOTHING
            """;
        try (Connection conn = dataSource.getConnection();
             PreparedStatement ps = conn.prepareStatement(sql)) {
            ps.setObject(1, row.requestId());
            ps.setString(2, row.gameUrl());
            ps.setString(3, row.platform());
            ps.setString(4, row.whiteUsername());
            ps.setString(5, row.blackUsername());
            setIntOrNull(ps, 6, row.whiteElo());
            setIntOrNull(ps, 7, row.blackElo());
            ps.setString(8, row.timeClass());
            ps.setString(9, row.eco());
            ps.setString(10, row.result());
            ps.setTimestamp(11, row.playedAt() != null ? Timestamp.from(row.playedAt()) : null);
            setIntOrNull(ps, 12, row.numMoves());
            ps.setBoolean(13, row.hasPin());
            ps.setBoolean(14, row.hasCrossPin());
            ps.setBoolean(15, row.hasFork());
            ps.setBoolean(16, row.hasSkewer());
            ps.setBoolean(17, row.hasDiscoveredAttack());
            ps.setString(18, row.motifsJson());
            ps.setString(19, row.pgn());
            ps.executeUpdate();
        } catch (SQLException e) {
            LOG.error("Failed to insert game feature for game_url={}", row.gameUrl(), e);
            throw new RuntimeException("Failed to insert game feature", e);
        }
    }

    public List<GameFeatureRow> query(CompiledQuery compiledQuery, int limit, int offset) {
        String sql = "SELECT * FROM game_features WHERE " + compiledQuery.sql()
                + " LIMIT ? OFFSET ?";
        try (Connection conn = dataSource.getConnection();
             PreparedStatement ps = conn.prepareStatement(sql)) {
            int idx = 1;
            for (Object param : compiledQuery.parameters()) {
                ps.setObject(idx++, param);
            }
            ps.setInt(idx++, limit);
            ps.setInt(idx, offset);

            try (ResultSet rs = ps.executeQuery()) {
                List<GameFeatureRow> results = new ArrayList<>();
                while (rs.next()) {
                    results.add(mapRow(rs));
                }
                return results;
            }
        } catch (SQLException e) {
            throw new RuntimeException("Failed to query game features", e);
        }
    }

    private GameFeatureRow mapRow(ResultSet rs) throws SQLException {
        return new GameFeatureRow(
                UUID.fromString(rs.getString("id")),
                UUID.fromString(rs.getString("request_id")),
                rs.getString("game_url"),
                rs.getString("platform"),
                rs.getString("white_username"),
                rs.getString("black_username"),
                getIntOrNull(rs, "white_elo"),
                getIntOrNull(rs, "black_elo"),
                rs.getString("time_class"),
                rs.getString("eco"),
                rs.getString("result"),
                rs.getTimestamp("played_at") != null ? rs.getTimestamp("played_at").toInstant() : null,
                getIntOrNull(rs, "num_moves"),
                rs.getBoolean("has_pin"),
                rs.getBoolean("has_cross_pin"),
                rs.getBoolean("has_fork"),
                rs.getBoolean("has_skewer"),
                rs.getBoolean("has_discovered_attack"),
                rs.getString("motifs_json"),
                rs.getString("pgn")
        );
    }

    private static void setIntOrNull(PreparedStatement ps, int idx, Integer value) throws SQLException {
        if (value != null) {
            ps.setInt(idx, value);
        } else {
            ps.setNull(idx, Types.INTEGER);
        }
    }

    private static Integer getIntOrNull(ResultSet rs, String column) throws SQLException {
        int val = rs.getInt(column);
        return rs.wasNull() ? null : val;
    }

    public record GameFeatureRow(
            UUID id,
            UUID requestId,
            String gameUrl,
            String platform,
            String whiteUsername,
            String blackUsername,
            Integer whiteElo,
            Integer blackElo,
            String timeClass,
            String eco,
            String result,
            java.time.Instant playedAt,
            Integer numMoves,
            boolean hasPin,
            boolean hasCrossPin,
            boolean hasFork,
            boolean hasSkewer,
            boolean hasDiscoveredAttack,
            String motifsJson,
            String pgn
    ) {}
}
