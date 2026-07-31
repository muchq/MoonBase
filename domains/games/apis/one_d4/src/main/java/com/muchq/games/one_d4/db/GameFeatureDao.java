package com.muchq.games.one_d4.db;

import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.one_d4.api.dto.AggregateRow;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.time.Clock;
import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneOffset;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
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
          white_elo, black_elo, white_title, black_title, time_class, eco,
          opening_name, opening_family, result, played_at, num_moves,
          indexed_at, pgn
      ) KEY (game_url) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
      """;

  // On conflict, refresh the derived/enriched columns too so that reindexing a period backfills
  // titles and opening names on rows indexed before those columns existed.
  private static final String PG_INSERT =
      """
      INSERT INTO game_features (
          request_id, game_url, platform, white_username, black_username,
          white_elo, black_elo, white_title, black_title, time_class, eco,
          opening_name, opening_family, result, played_at, num_moves,
          indexed_at, pgn
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
      ON CONFLICT (game_url) DO UPDATE SET
          indexed_at = EXCLUDED.indexed_at,
          request_id = EXCLUDED.request_id,
          white_title = EXCLUDED.white_title,
          black_title = EXCLUDED.black_title,
          opening_name = EXCLUDED.opening_name,
          opening_family = EXCLUDED.opening_family
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
              rs.getString("white_title"),
              rs.getString("black_title"),
              rs.getString("time_class"),
              rs.getString("eco"),
              rs.getString("opening_name"),
              rs.getString("opening_family"),
              rs.getString("result"),
              fromUtcWallClock(rs.getObject("played_at", LocalDateTime.class)),
              getIntOrNull(rs, "num_moves"),
              fromUtcWallClock(rs.getObject("indexed_at", LocalDateTime.class)),
              rs.getString("pgn"));

  private static final RowMapper<GameForReanalysis> REANALYSIS_MAPPER =
      (rs, ctx) ->
          new GameForReanalysis(
              UUID.fromString(rs.getString("request_id")),
              rs.getString("game_url"),
              rs.getString("pgn"));

  private final Jdbi jdbi;
  private final boolean useH2;
  private final Clock clock;

  public GameFeatureDao(Jdbi jdbi, boolean useH2) {
    this(jdbi, useH2, Clock.systemUTC());
  }

  /**
   * @param clock stamps {@code indexed_at} when a caller supplies a row without one. Retention
   *     compares that column against a threshold this same clock produces, so both sides have to
   *     come from one source; see {@link #deleteOlderThan}.
   */
  public GameFeatureDao(Jdbi jdbi, boolean useH2, Clock clock) {
    this.jdbi = jdbi;
    this.useH2 = useH2;
    this.clock = clock;
  }

  /**
   * played_at is TIMESTAMP WITHOUT TIME ZONE on both H2 and Postgres, so the column holds a wall
   * clock rather than an instant — and {@link LocalDateTime} is exactly that type. Binding and
   * reading it as a LocalDateTime is a straight JDBC 4.2 mapping onto the column: no zone
   * conversion happens on either side, so there is no zone to get wrong and nothing for a caller to
   * remember to pass. Modelling it as an {@link Instant} instead would drag the JVM default zone
   * into every bind, making the stored value depend on where the process happened to run — a game
   * written under UTC and read back under America/Los_Angeles would land on the wrong calendar day
   * and drop out of {@code month = "2026-06"}.
   *
   * <p>These two helpers encode the one convention the type cannot: the stored wall clock is UTC.
   * ChessQL's date/month rewrite emits its day and month boundaries as zone-free LocalDateTimes on
   * that same convention, so filters compare like against like — and because both sides are already
   * the column's own type, nothing has to be threaded through the bind sites to keep them agreeing.
   * Under a UTC JVM this is a no-op against the older Calendar-based binding — the conversion that
   * forced was already the identity — so existing rows keep their exact stored values.
   */
  private static @Nullable LocalDateTime toUtcWallClock(@Nullable Instant instant) {
    return instant == null ? null : instant.atOffset(ZoneOffset.UTC).toLocalDateTime();
  }

  /** Inverse of {@link #toUtcWallClock}: a stored UTC wall clock read back as an instant. */
  private static @Nullable Instant fromUtcWallClock(@Nullable LocalDateTime wallClock) {
    return wallClock == null ? null : wallClock.toInstant(ZoneOffset.UTC);
  }

  @Override
  public void insertBatch(List<GameFeature> features) {
    if (features.isEmpty()) return;
    String sql = useH2 ? H2_INSERT : PG_INSERT;
    jdbi.useHandle(
        h -> {
          var batch = h.prepareBatch(sql);
          for (GameFeature row : features) {
            // bindByType, not bind: played_at is nullable, and only the typed form carries
            // Types.TIMESTAMP into setNull. Plain bind() would fall back to JDBI's untyped-null
            // default (Types.OTHER) — accepted by both drivers today, but not the column's type.
            batch
                .bind(0, row.requestId())
                .bind(1, row.gameUrl())
                .bind(2, row.platform())
                .bind(3, row.whiteUsername())
                .bind(4, row.blackUsername())
                .bind(5, (Integer) row.whiteElo())
                .bind(6, (Integer) row.blackElo())
                .bind(7, row.whiteTitle())
                .bind(8, row.blackTitle())
                .bind(9, row.timeClass())
                .bind(10, row.eco())
                .bind(11, row.openingName())
                .bind(12, row.openingFamily())
                .bind(13, row.result())
                .bindByType(14, toUtcWallClock(row.playedAt()), LocalDateTime.class)
                .bind(15, (Integer) row.numMoves())
                // Never null: the column is NOT NULL, and a row that slipped through without a
                // stamp would be undeletable, since `NULL < threshold` is unknown.
                .bindByType(
                    16,
                    toUtcWallClock(row.indexedAt() == null ? clock.instant() : row.indexedAt()),
                    LocalDateTime.class)
                .bind(17, row.pgn())
                .add();
          }
          batch.execute();
        });
  }

  /**
   * Both sides of this comparison are UTC wall clocks written by the JVM: {@code indexed_at} is
   * bound by {@link #insertBatch} from the caller's clock, and the threshold is bound here on the
   * same convention. Previously {@code indexed_at} came from the database's own {@code now()} while
   * the threshold came from the JVM, so retention straddled two clocks and drifted with host skew
   * and the database server's timezone (#1268). Nothing in the column changed for a UTC server —
   * {@code now()} was already producing a UTC wall clock there — so existing rows keep their exact
   * stored values.
   */
  @Override
  public int deleteOlderThan(Instant threshold) {
    return jdbi.withHandle(
        h -> {
          int deleted =
              h.createUpdate("DELETE FROM game_features WHERE indexed_at < ?")
                  .bindByType(0, toUtcWallClock(threshold), LocalDateTime.class)
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
  public List<GameFeature> query(Object compiledQuery, int limit, int offset) {
    if (!(compiledQuery instanceof CompiledQuery cq)) {
      throw new IllegalArgumentException(
          "Expected CompiledQuery, got: " + compiledQuery.getClass());
    }
    String sql = cq.selectSql() + " LIMIT ? OFFSET ?";
    return jdbi.withHandle(
        h -> {
          var query = h.createQuery(sql);
          int idx = 0;
          for (Object param : cq.parameters()) {
            query.bind(idx++, param);
          }
          query.bind(idx++, limit);
          query.bind(idx, offset);
          return query.map(GAME_FEATURE_MAPPER).list();
        });
  }

  @Override
  public List<AggregateRow> aggregate(Object compiledQuery, List<String> groupColumns, int limit) {
    if (!(compiledQuery instanceof CompiledQuery cq)) {
      throw new IllegalArgumentException(
          "Expected CompiledQuery, got: " + compiledQuery.getClass());
    }
    String sql = cq.selectSql() + " LIMIT ?";
    return jdbi.withHandle(
        h -> {
          var query = h.createQuery(sql);
          int idx = 0;
          for (Object param : cq.parameters()) {
            query.bind(idx++, param);
          }
          query.bind(idx, limit);
          return query
              .map(
                  (rs, ctx) -> {
                    Map<String, Object> group = new LinkedHashMap<>();
                    for (String column : groupColumns) {
                      group.put(column, rs.getObject(column));
                    }
                    return new AggregateRow(group, rs.getLong("group_count"));
                  })
              .list();
        });
  }

  @Override
  public AggregateTotals aggregateTotals(Object compiledQuery) {
    if (!(compiledQuery instanceof CompiledQuery cq)) {
      throw new IllegalArgumentException(
          "Expected CompiledQuery, got: " + compiledQuery.getClass());
    }
    return jdbi.withHandle(
        h -> {
          var query = h.createQuery(cq.selectSql());
          int idx = 0;
          for (Object param : cq.parameters()) {
            query.bind(idx++, param);
          }
          return query
              .map(
                  (rs, ctx) ->
                      new AggregateTotals(rs.getLong("total_games"), rs.getLong("total_groups")))
              .one();
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
