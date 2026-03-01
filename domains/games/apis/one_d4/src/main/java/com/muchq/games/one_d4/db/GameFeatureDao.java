package com.muchq.games.one_d4.db;

import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Timestamp;
import java.sql.Types;
import java.time.Instant;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
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
          indexed_at, pgn
      ) KEY (game_url) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, now(), ?)
      """;

  private static final String PG_INSERT =
      """
      INSERT INTO game_features (
          request_id, game_url, platform, white_username, black_username,
          white_elo, black_elo, time_class, eco, result, played_at, num_moves,
          indexed_at, pgn
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, now(), ?)
      ON CONFLICT (game_url) DO UPDATE SET
          indexed_at = EXCLUDED.indexed_at,
          request_id = EXCLUDED.request_id
      """;

  private static final String FETCH_FOR_REANALYSIS =
      "SELECT request_id, game_url, pgn FROM game_features ORDER BY indexed_at LIMIT ? OFFSET ?";

  private static final String DELETE_OCCURRENCES_BY_GAME_URL =
      "DELETE FROM motif_occurrences WHERE game_url = ?";

  private static final String INSERT_OCCURRENCE =
      "INSERT INTO motif_occurrences"
          + " (id, game_url, motif, ply, side, move_number, description,"
          + " moved_piece, attacker, target, is_discovered, is_mate, pin_type)"
          + " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

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
      // indexed_at set via now() in SQL — no parameter
      ps.setString(13, row.pgn());
      ps.executeUpdate();
    } catch (SQLException e) {
      LOG.error("Failed to insert game feature for game_url={}", row.gameUrl(), e);
      throw new RuntimeException("Failed to insert game feature", e);
    }
  }

  @Override
  public int deleteOlderThan(Instant threshold) {
    String sql = "DELETE FROM game_features WHERE indexed_at < ?";
    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps = conn.prepareStatement(sql)) {
      ps.setTimestamp(1, Timestamp.from(threshold));
      int deleted = ps.executeUpdate();
      if (deleted > 0) {
        LOG.debug("Deleted {} games older than {}", deleted, threshold);
      }
      return deleted;
    } catch (SQLException e) {
      throw new RuntimeException("Failed to delete old games", e);
    }
  }

  @Override
  public void insertOccurrences(
      String gameUrl, Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences) {
    if (occurrences.isEmpty()) return;
    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps = conn.prepareStatement(INSERT_OCCURRENCE)) {
      for (Map.Entry<Motif, List<GameFeatures.MotifOccurrence>> entry : occurrences.entrySet()) {
        String motifName = entry.getKey().name();
        for (GameFeatures.MotifOccurrence occ : entry.getValue()) {
          if (occ.ply() <= 0) continue;
          ps.setString(1, UUID.randomUUID().toString());
          ps.setString(2, gameUrl);
          ps.setString(3, motifName);
          ps.setInt(4, occ.ply());
          ps.setString(5, occ.side());
          ps.setInt(6, occ.moveNumber());
          ps.setString(7, occ.description());
          ps.setString(8, occ.movedPiece());
          ps.setString(9, occ.attacker());
          ps.setString(10, occ.target());
          ps.setBoolean(11, occ.isDiscovered());
          ps.setBoolean(12, occ.isMate());
          ps.setString(13, occ.pinType());
          ps.addBatch();
        }
      }
      ps.executeBatch();
    } catch (SQLException e) {
      LOG.error("Failed to insert motif occurrences for game_url={}", gameUrl, e);
      throw new RuntimeException("Failed to insert motif occurrences", e);
    }
  }

  @Override
  public List<GameFeature> query(Object compiledQuery, int limit, int offset) {
    if (!(compiledQuery instanceof CompiledQuery cq)) {
      throw new IllegalArgumentException(
          "Expected CompiledQuery, got: " + compiledQuery.getClass());
    }
    String sql = cq.selectSql() + " LIMIT ? OFFSET ?";
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

  @Override
  public Map<String, Map<String, List<OccurrenceRow>>> queryOccurrences(List<String> gameUrls) {
    if (gameUrls.isEmpty()) return Map.of();
    String placeholders = gameUrls.stream().map(u -> "?").collect(Collectors.joining(", "));
    // Fetch all rows including ATTACK (needed for fork derivation) but excluding stale
    // materialized FORK rows (derived motifs must not be double-counted).
    String sql =
        "SELECT game_url, motif, move_number, side, description,"
            + " moved_piece, attacker, target, is_discovered, is_mate, pin_type"
            + " FROM motif_occurrences WHERE game_url IN ("
            + placeholders
            + ") AND motif != 'FORK' ORDER BY ply ASC";
    Map<String, Map<String, List<OccurrenceRow>>> result = new LinkedHashMap<>();
    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps = conn.prepareStatement(sql)) {
      int idx = 1;
      for (String url : gameUrls) {
        ps.setString(idx++, url);
      }
      try (ResultSet rs = ps.executeQuery()) {
        while (rs.next()) {
          String gameUrl = rs.getString("game_url");
          // Store motif key as lowercase to match ChessQL motif naming convention
          String motif = rs.getString("motif").toLowerCase();
          int moveNumber = rs.getInt("move_number");
          String side = rs.getString("side");
          String description = rs.getString("description");
          String movedPiece = rs.getString("moved_piece");
          String attacker = rs.getString("attacker");
          String target = rs.getString("target");
          boolean isDiscovered = rs.getBoolean("is_discovered");
          boolean isMate = rs.getBoolean("is_mate");
          String pinType = rs.getString("pin_type");
          result
              .computeIfAbsent(gameUrl, k -> new LinkedHashMap<>())
              .computeIfAbsent(motif, k -> new ArrayList<>())
              .add(
                  new OccurrenceRow(
                      gameUrl,
                      motif,
                      moveNumber,
                      side,
                      description,
                      movedPiece,
                      attacker,
                      target,
                      isDiscovered,
                      isMate,
                      pinType));
        }
      }
    } catch (SQLException e) {
      throw new RuntimeException("Failed to query motif occurrences", e);
    }

    // Post-process: derive FORK from ATTACK rows, then remove ATTACK (internal primitive).
    for (Map.Entry<String, Map<String, List<OccurrenceRow>>> entry : result.entrySet()) {
      Map<String, List<OccurrenceRow>> motifMap = entry.getValue();
      List<OccurrenceRow> attackOccs = motifMap.getOrDefault("attack", List.of());
      List<OccurrenceRow> forkOccs = deriveForkOccurrences(entry.getKey(), attackOccs);
      if (!forkOccs.isEmpty()) {
        motifMap.put("fork", forkOccs);
      }
      motifMap.remove("attack");
    }

    return result;
  }

  /**
   * Derives FORK occurrences from a game's ATTACK rows. Groups non-discovered ATTACK rows by
   * (moveNumber, side, attacker); groups with 2+ distinct targets constitute a fork. Mirrors the
   * SQL derivation in {@code SqlCompiler.compileMotif("fork")}.
   */
  private static List<OccurrenceRow> deriveForkOccurrences(
      String gameUrl, List<OccurrenceRow> attackOccs) {
    Map<String, List<OccurrenceRow>> groups = new LinkedHashMap<>();
    for (OccurrenceRow occ : attackOccs) {
      if (occ.attacker() == null || occ.isDiscovered()) continue;
      String key = occ.moveNumber() + "|" + occ.side() + "|" + occ.attacker();
      groups.computeIfAbsent(key, k -> new ArrayList<>()).add(occ);
    }
    List<OccurrenceRow> forkOccs = new ArrayList<>();
    for (List<OccurrenceRow> group : groups.values()) {
      if (group.size() >= 2) {
        for (OccurrenceRow attackOcc : group) {
          forkOccs.add(
              new OccurrenceRow(
                  gameUrl,
                  "fork",
                  attackOcc.moveNumber(),
                  attackOcc.side(),
                  "Fork at move " + attackOcc.moveNumber(),
                  attackOcc.movedPiece(),
                  attackOcc.attacker(),
                  attackOcc.target(),
                  false,
                  false,
                  null));
        }
      }
    }
    return forkOccs;
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
        rs.getTimestamp("indexed_at") != null ? rs.getTimestamp("indexed_at").toInstant() : null,
        rs.getString("pgn"));
  }

  @Override
  public void deleteOccurrencesByGameUrl(String gameUrl) {
    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps = conn.prepareStatement(DELETE_OCCURRENCES_BY_GAME_URL)) {
      ps.setString(1, gameUrl);
      ps.executeUpdate();
    } catch (SQLException e) {
      LOG.error("Failed to delete occurrences for game_url={}", gameUrl, e);
      throw new RuntimeException("Failed to delete occurrences", e);
    }
  }

  @Override
  public List<GameForReanalysis> fetchForReanalysis(int limit, int offset) {
    List<GameForReanalysis> results = new ArrayList<>();
    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps = conn.prepareStatement(FETCH_FOR_REANALYSIS)) {
      ps.setInt(1, limit);
      ps.setInt(2, offset);
      try (ResultSet rs = ps.executeQuery()) {
        while (rs.next()) {
          UUID requestId = UUID.fromString(rs.getString("request_id"));
          String gameUrl = rs.getString("game_url");
          String pgn = rs.getString("pgn");
          results.add(new GameForReanalysis(requestId, gameUrl, pgn));
        }
      }
    } catch (SQLException e) {
      throw new RuntimeException("Failed to fetch games for reanalysis", e);
    }
    return results;
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
