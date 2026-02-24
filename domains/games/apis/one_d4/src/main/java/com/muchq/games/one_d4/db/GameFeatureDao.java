package com.muchq.games.one_d4.db;

import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.one_d4.api.dto.GameFeature;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Timestamp;
import java.sql.Types;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import javax.sql.DataSource;
import org.jspecify.annotations.Nullable;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class GameFeatureDao implements GameFeatureStore {
  private static final Logger LOG = LoggerFactory.getLogger(GameFeatureDao.class);

  private static final String H2_INSERT =
      """
      MERGE INTO game_features (
          request_id, game_url, platform, white_username, black_username,
          white_elo, black_elo, time_class, eco, result, played_at, num_moves,
          has_pin, has_cross_pin, has_fork, has_skewer, has_discovered_attack,
          has_check, has_checkmate, has_promotion, has_promotion_with_check, has_promotion_with_checkmate,
          motifs_json, pgn
      ) KEY (game_url) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
      """;

  private static final String PG_INSERT =
      """
      INSERT INTO game_features (
          request_id, game_url, platform, white_username, black_username,
          white_elo, black_elo, time_class, eco, result, played_at, num_moves,
          has_pin, has_cross_pin, has_fork, has_skewer, has_discovered_attack,
          has_check, has_checkmate, has_promotion, has_promotion_with_check, has_promotion_with_checkmate,
          motifs_json, pgn
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?::jsonb, ?)
      ON CONFLICT (game_url) DO NOTHING
      """;

  private final DataSource dataSource;
  private final boolean useH2;

  public GameFeatureDao(DataSource dataSource, boolean useH2) {
    this.dataSource = dataSource;
    this.useH2 = useH2;
  }

  @Override
  public void insert(GameFeature row) {
    String sql = useH2 ? H2_INSERT : PG_INSERT;
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
      ps.setBoolean(18, row.hasCheck());
      ps.setBoolean(19, row.hasCheckmate());
      ps.setBoolean(20, row.hasPromotion());
      ps.setBoolean(21, row.hasPromotionWithCheck());
      ps.setBoolean(22, row.hasPromotionWithCheckmate());
      ps.setString(23, row.motifsJson());
      ps.setString(24, row.pgn());
      ps.executeUpdate();
    } catch (SQLException e) {
      LOG.error("Failed to insert game feature for game_url={}", row.gameUrl(), e);
      throw new RuntimeException("Failed to insert game feature", e);
    }
  }

  @Override
  public List<GameFeature> query(Object compiledQuery, int limit, int offset) {
    if (!(compiledQuery instanceof CompiledQuery cq)) {
      throw new IllegalArgumentException(
          "Expected CompiledQuery, got: " + compiledQuery.getClass());
    }
    String sql =
        "SELECT * FROM game_features WHERE "
            + cq.sql()
            + " ORDER BY played_at DESC LIMIT ? OFFSET ?";
    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps = conn.prepareStatement(sql)) {
      int idx = 1;
      for (Object param : cq.parameters()) {
        ps.setObject(idx++, param);
      }
      ps.setInt(idx++, limit);
      ps.setInt(idx, offset);

      try (ResultSet rs = ps.executeQuery()) {
        List<GameFeature> results = new ArrayList<>();
        while (rs.next()) {
          results.add(mapRow(rs));
        }
        return results;
      }
    } catch (SQLException e) {
      throw new RuntimeException("Failed to query game features", e);
    }
  }

  private GameFeature mapRow(ResultSet rs) throws SQLException {
    return new GameFeature(
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
        rs.getTimestamp("played_at").toInstant(),
        getIntOrNull(rs, "num_moves"),
        rs.getBoolean("has_pin"),
        rs.getBoolean("has_cross_pin"),
        rs.getBoolean("has_fork"),
        rs.getBoolean("has_skewer"),
        rs.getBoolean("has_discovered_attack"),
        rs.getBoolean("has_check"),
        rs.getBoolean("has_checkmate"),
        rs.getBoolean("has_promotion"),
        rs.getBoolean("has_promotion_with_check"),
        rs.getBoolean("has_promotion_with_checkmate"),
        rs.getString("motifs_json"),
        rs.getString("pgn"));
  }

  private static void setIntOrNull(PreparedStatement ps, int idx, Integer value)
      throws SQLException {
    if (value != null) {
      ps.setInt(idx, value);
    } else {
      ps.setNull(idx, Types.INTEGER);
    }
  }

  private static @Nullable Integer getIntOrNull(ResultSet rs, String column) throws SQLException {
    int val = rs.getInt(column);
    return rs.wasNull() ? null : val;
  }
}
