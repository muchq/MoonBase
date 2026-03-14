package com.muchq.games.one_d4.db;

import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Timestamp;
import java.time.Instant;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import org.jdbi.v3.core.Jdbi;
import org.jdbi.v3.core.mapper.RowMapper;
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
      "SELECT request_id, game_url, pgn FROM game_features ORDER BY indexed_at, game_url LIMIT ?"
          + " OFFSET ?";

  private static final String DELETE_OCCURRENCES_BY_GAME_URL =
      "DELETE FROM motif_occurrences WHERE game_url = ?";

  private static final String INSERT_OCCURRENCE =
      "INSERT INTO motif_occurrences"
          + " (id, game_url, motif, ply, side, move_number, description,"
          + " moved_piece, attacker, target, is_discovered, is_mate, pin_type)"
          + " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

  private static final String QUERY_OCCURRENCES =
      "SELECT game_url, motif, move_number, side, description,"
          + " moved_piece, attacker, target, is_discovered, is_mate, pin_type"
          + " FROM motif_occurrences WHERE game_url IN (<urls>)"
          + " AND motif NOT IN"
          + " ('FORK', 'CHECKMATE', 'DISCOVERED_CHECK', 'DOUBLE_CHECK', 'DISCOVERED_ATTACK')"
          + " ORDER BY ply ASC";

  /** Column list for list queries that omit PGN to reduce payload and DB read. */
  private static final String LIST_COLUMNS_NO_PGN =
      "g.id, g.request_id, g.game_url, g.platform, g.white_username, g.black_username,"
          + " g.white_elo, g.black_elo, g.time_class, g.eco, g.result, g.played_at, g.num_moves,"
          + " g.indexed_at";

  private static final RowMapper<GameFeature> GAME_FEATURE_MAPPER =
      (rs, ctx) ->
          new GameFeature(
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
              rs.getTimestamp("indexed_at") != null
                  ? rs.getTimestamp("indexed_at").toInstant()
                  : null,
              rs.getString("pgn"));

  private static final RowMapper<GameFeature> GAME_FEATURE_MAPPER_NO_PGN =
      (rs, ctx) ->
          new GameFeature(
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
              rs.getTimestamp("indexed_at") != null
                  ? rs.getTimestamp("indexed_at").toInstant()
                  : null,
              null);

  private static final RowMapper<GameForReanalysis> REANALYSIS_MAPPER =
      (rs, ctx) ->
          new GameForReanalysis(
              UUID.fromString(rs.getString("request_id")),
              rs.getString("game_url"),
              rs.getString("pgn"));

  private static final String FIND_BY_GAME_URL =
      "SELECT id, request_id, game_url, platform, white_username, black_username,"
          + " white_elo, black_elo, time_class, eco, result, played_at, num_moves, indexed_at, pgn"
          + " FROM game_features WHERE game_url = ?";

  private final Jdbi jdbi;
  private final boolean useH2;

  public GameFeatureDao(Jdbi jdbi, boolean useH2) {
    this.jdbi = jdbi;
    this.useH2 = useH2;
  }

  @Override
  public Optional<GameFeature> findByGameUrl(String gameUrl) {
    List<GameFeature> list =
        jdbi.withHandle(
            h -> h.createQuery(FIND_BY_GAME_URL).bind(0, gameUrl).map(GAME_FEATURE_MAPPER).list());
    return list.isEmpty() ? Optional.empty() : Optional.of(list.get(0));
  }

  @Override
  public void insertBatch(List<GameFeature> features) {
    if (features.isEmpty()) return;
    String sql = useH2 ? H2_INSERT : PG_INSERT;
    jdbi.useHandle(
        h -> {
          var batch = h.prepareBatch(sql);
          for (GameFeature row : features) {
            batch
                .bind(0, row.requestId())
                .bind(1, row.gameUrl())
                .bind(2, row.platform())
                .bind(3, row.whiteUsername())
                .bind(4, row.blackUsername())
                .bind(5, (Integer) row.whiteElo())
                .bind(6, (Integer) row.blackElo())
                .bind(7, row.timeClass())
                .bind(8, row.eco())
                .bind(9, row.result())
                .bind(10, row.playedAt() != null ? Timestamp.from(row.playedAt()) : null)
                .bind(11, (Integer) row.numMoves())
                .bind(12, row.pgn())
                .add();
          }
          batch.execute();
        });
  }

  @Override
  public int deleteOlderThan(Instant threshold) {
    return jdbi.withHandle(
        h -> {
          int deleted =
              h.createUpdate("DELETE FROM game_features WHERE indexed_at < ?")
                  .bind(0, Timestamp.from(threshold))
                  .execute();
          if (deleted > 0) {
            LOG.debug("Deleted {} games older than {}", deleted, threshold);
          }
          return deleted;
        });
  }

  @Override
  public void insertOccurrencesBatch(
      Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame) {
    if (occurrencesByGame.isEmpty()) return;
    jdbi.useHandle(
        h -> {
          var batch = h.prepareBatch(INSERT_OCCURRENCE);
          for (var gameEntry : occurrencesByGame.entrySet()) {
            String gameUrl = gameEntry.getKey();
            for (var motifEntry : gameEntry.getValue().entrySet()) {
              String motifName = motifEntry.getKey().name();
              for (GameFeatures.MotifOccurrence occ : motifEntry.getValue()) {
                if (occ.ply() <= 0) continue;
                batch
                    .bind(0, UUID.randomUUID().toString())
                    .bind(1, gameUrl)
                    .bind(2, motifName)
                    .bind(3, occ.ply())
                    .bind(4, occ.side())
                    .bind(5, occ.moveNumber())
                    .bind(6, occ.description())
                    .bind(7, occ.movedPiece())
                    .bind(8, occ.attacker())
                    .bind(9, occ.target())
                    .bind(10, occ.isDiscovered())
                    .bind(11, occ.isMate())
                    .bind(12, occ.pinType())
                    .add();
              }
            }
          }
          batch.execute();
        });
  }

  @Override
  public List<GameFeature> query(Object compiledQuery, int limit, int offset, boolean includePgn) {
    if (!(compiledQuery instanceof CompiledQuery cq)) {
      throw new IllegalArgumentException(
          "Expected CompiledQuery, got: " + compiledQuery.getClass());
    }
    String selectSql = cq.selectSql();
    String listSql;
    RowMapper<GameFeature> mapper;
    if (includePgn) {
      listSql = selectSql + " LIMIT ? OFFSET ?";
      mapper = GAME_FEATURE_MAPPER;
    } else {
      listSql =
          selectSql.replace("SELECT g.* FROM", "SELECT " + LIST_COLUMNS_NO_PGN + " FROM")
              + " LIMIT ? OFFSET ?";
      mapper = GAME_FEATURE_MAPPER_NO_PGN;
    }
    return jdbi.withHandle(
        h -> {
          var query = h.createQuery(listSql);
          int idx = 0;
          for (Object param : cq.parameters()) {
            query.bind(idx++, param);
          }
          query.bind(idx++, limit);
          query.bind(idx, offset);
          return query.map(mapper).list();
        });
  }

  @Override
  public int count(Object compiledQuery) {
    if (!(compiledQuery instanceof CompiledQuery cq)) {
      throw new IllegalArgumentException(
          "Expected CompiledQuery, got: " + compiledQuery.getClass());
    }
    String selectSql = cq.selectSql();
    // Strip the outer ORDER BY (use lastIndexOf in case ORDER BY appears in a subquery).
    int orderByIndex = selectSql.lastIndexOf(" ORDER BY ");
    String sqlWithoutOrder = orderByIndex >= 0 ? selectSql.substring(0, orderByIndex) : selectSql;
    String countSql = sqlWithoutOrder.replaceFirst("SELECT g\\.\\* FROM", "SELECT COUNT(*) FROM");
    return jdbi.withHandle(
        h -> {
          var query = h.createQuery(countSql);
          int idx = 0;
          for (Object param : cq.parameters()) {
            query.bind(idx++, param);
          }
          return query.mapTo(Integer.class).one();
        });
  }

  @Override
  public Map<String, Map<String, List<OccurrenceRow>>> queryOccurrences(List<String> gameUrls) {
    if (gameUrls.isEmpty()) return Map.of();
    // Fetch all rows including ATTACK (needed for derivation) but excluding stale materialized
    // rows for motifs now derived at response time. ATTACK itself is removed in post-processing.
    Map<String, Map<String, List<OccurrenceRow>>> result =
        jdbi.withHandle(
            h -> {
              var rows =
                  h.createQuery(QUERY_OCCURRENCES)
                      .bindList("urls", gameUrls)
                      .map(
                          (rs, ctx) -> {
                            String gameUrl = rs.getString("game_url");
                            // Store motif key as lowercase to match ChessQL motif naming convention
                            String motif = rs.getString("motif").toLowerCase();
                            return new OccurrenceRow(
                                gameUrl,
                                motif,
                                rs.getInt("move_number"),
                                rs.getString("side"),
                                rs.getString("description"),
                                rs.getString("moved_piece"),
                                rs.getString("attacker"),
                                rs.getString("target"),
                                rs.getBoolean("is_discovered"),
                                rs.getBoolean("is_mate"),
                                rs.getString("pin_type"));
                          })
                      .list();
              Map<String, Map<String, List<OccurrenceRow>>> grouped = new LinkedHashMap<>();
              for (OccurrenceRow row : rows) {
                grouped
                    .computeIfAbsent(row.gameUrl(), k -> new LinkedHashMap<>())
                    .computeIfAbsent(row.motif(), k -> new ArrayList<>())
                    .add(row);
              }
              return grouped;
            });

    // Post-process: derive all ATTACK-based motifs, then remove ATTACK (internal primitive).
    for (Map.Entry<String, Map<String, List<OccurrenceRow>>> entry : result.entrySet()) {
      Map<String, List<OccurrenceRow>> motifMap = entry.getValue();
      String gameUrl = entry.getKey();
      List<OccurrenceRow> attackOccs = motifMap.getOrDefault("attack", List.of());

      addIfNonEmpty(motifMap, "fork", deriveForkOccurrences(gameUrl, attackOccs));
      addIfNonEmpty(
          motifMap, "discovered_attack", deriveDiscoveredAttackOccurrences(gameUrl, attackOccs));
      addIfNonEmpty(motifMap, "checkmate", deriveCheckmateOccurrences(gameUrl, attackOccs));
      addIfNonEmpty(
          motifMap, "discovered_check", deriveDiscoveredCheckOccurrences(gameUrl, attackOccs));
      addIfNonEmpty(motifMap, "double_check", deriveDoubleCheckOccurrences(gameUrl, attackOccs));

      motifMap.remove("attack");
    }

    return result;
  }

  private static void addIfNonEmpty(
      Map<String, List<OccurrenceRow>> motifMap, String key, List<OccurrenceRow> occs) {
    if (!occs.isEmpty()) motifMap.put(key, occs);
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

  /** Derives DISCOVERED_ATTACK occurrences from ATTACK rows with {@code isDiscovered = true}. */
  private static List<OccurrenceRow> deriveDiscoveredAttackOccurrences(
      String gameUrl, List<OccurrenceRow> attackOccs) {
    List<OccurrenceRow> result = new ArrayList<>();
    for (OccurrenceRow occ : attackOccs) {
      if (occ.isDiscovered()) {
        result.add(
            new OccurrenceRow(
                gameUrl,
                "discovered_attack",
                occ.moveNumber(),
                occ.side(),
                occ.description(),
                occ.movedPiece(),
                occ.attacker(),
                occ.target(),
                true,
                occ.isMate(),
                null));
      }
    }
    return result;
  }

  /**
   * Derives CHECKMATE occurrences from ATTACK rows with {@code isMate = true}. Mirrors {@code
   * SqlCompiler.compileMotif("checkmate")}.
   */
  private static List<OccurrenceRow> deriveCheckmateOccurrences(
      String gameUrl, List<OccurrenceRow> attackOccs) {
    List<OccurrenceRow> result = new ArrayList<>();
    for (OccurrenceRow occ : attackOccs) {
      if (occ.isMate()) {
        result.add(
            new OccurrenceRow(
                gameUrl,
                "checkmate",
                occ.moveNumber(),
                occ.side(),
                "Checkmate at move " + occ.moveNumber(),
                occ.movedPiece(),
                occ.attacker(),
                occ.target(),
                false,
                true,
                null));
      }
    }
    return result;
  }

  /**
   * Derives DISCOVERED_CHECK occurrences from discovered ATTACK rows whose target is the king.
   * Mirrors {@code SqlCompiler.compileMotif("discovered_check")}.
   */
  private static List<OccurrenceRow> deriveDiscoveredCheckOccurrences(
      String gameUrl, List<OccurrenceRow> attackOccs) {
    List<OccurrenceRow> result = new ArrayList<>();
    for (OccurrenceRow occ : attackOccs) {
      if (occ.isDiscovered() && isKingTarget(occ.target())) {
        result.add(
            new OccurrenceRow(
                gameUrl,
                "discovered_check",
                occ.moveNumber(),
                occ.side(),
                "Discovered check at move " + occ.moveNumber(),
                occ.movedPiece(),
                occ.attacker(),
                occ.target(),
                true,
                occ.isMate(),
                null));
      }
    }
    return result;
  }

  /**
   * Derives DOUBLE_CHECK occurrences from positions where 2+ ATTACK rows target the king at the
   * same (moveNumber, side). Mirrors {@code SqlCompiler.compileMotif("double_check")}.
   */
  private static List<OccurrenceRow> deriveDoubleCheckOccurrences(
      String gameUrl, List<OccurrenceRow> attackOccs) {
    Map<String, List<OccurrenceRow>> groups = new LinkedHashMap<>();
    for (OccurrenceRow occ : attackOccs) {
      if (isKingTarget(occ.target())) {
        String key = occ.moveNumber() + "|" + occ.side();
        groups.computeIfAbsent(key, k -> new ArrayList<>()).add(occ);
      }
    }
    List<OccurrenceRow> result = new ArrayList<>();
    for (List<OccurrenceRow> group : groups.values()) {
      if (group.size() >= 2) {
        OccurrenceRow rep = group.get(0);
        result.add(
            new OccurrenceRow(
                gameUrl,
                "double_check",
                rep.moveNumber(),
                rep.side(),
                "Double check at move " + rep.moveNumber(),
                null,
                null,
                rep.target(),
                false,
                false,
                null));
      }
    }
    return result;
  }

  private static boolean isKingTarget(@Nullable String target) {
    return target != null && (target.startsWith("K") || target.startsWith("k"));
  }

  @Override
  public void deleteOccurrencesByGameUrls(List<String> gameUrls) {
    if (gameUrls.isEmpty()) return;
    jdbi.useHandle(
        h -> {
          var batch = h.prepareBatch(DELETE_OCCURRENCES_BY_GAME_URL);
          for (String gameUrl : gameUrls) {
            batch.bind(0, gameUrl).add();
          }
          batch.execute();
        });
  }

  @Override
  public List<GameForReanalysis> fetchForReanalysis(int limit, int offset) {
    return jdbi.withHandle(
        h ->
            h.createQuery(FETCH_FOR_REANALYSIS)
                .bind(0, limit)
                .bind(1, offset)
                .map(REANALYSIS_MAPPER)
                .list());
  }

  private static @Nullable Integer getIntOrNull(ResultSet rs, String column) throws SQLException {
    int val = rs.getInt(column);
    return rs.wasNull() ? null : val;
  }
}
