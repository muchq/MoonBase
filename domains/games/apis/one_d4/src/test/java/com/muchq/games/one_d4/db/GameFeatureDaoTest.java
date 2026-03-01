package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.db.GameFeatureStore.GameForReanalysis;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import javax.sql.DataSource;
import org.junit.Before;
import org.junit.Test;

public class GameFeatureDaoTest {

  private GameFeatureDao dao;
  private DataSource dataSource;
  private UUID requestId;

  @Before
  public void setUp() {
    String jdbcUrl =
        "jdbc:h2:mem:gamefeaturedao_" + System.currentTimeMillis() + ";DB_CLOSE_DELAY=-1";
    dataSource = DataSourceFactory.create(jdbcUrl, "sa", "");
    Migration migration = new Migration(dataSource, true);
    migration.run();

    dao = new GameFeatureDao(dataSource, true);
    requestId = UUID.randomUUID();

    try (var conn = dataSource.getConnection();
        var stmt =
            conn.prepareStatement(
                "INSERT INTO indexing_requests (id, player, platform, start_month, end_month,"
                    + " status) VALUES (?, 'p', 'CHESS_COM', '2024-01', '2024-01', 'COMPLETED')")) {
      stmt.setObject(1, requestId);
      stmt.executeUpdate();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  @Test
  public void insertOccurrences_and_queryOccurrences_roundTrip() {
    String gameUrl = "https://chess.com/game/occ-1";
    GameFeature game = createGame(gameUrl);
    dao.insert(game);

    GameFeatures.MotifOccurrence occ1 =
        new GameFeatures.MotifOccurrence(
            5, 3, "white", "Knight pinned on c6", null, null, null, false, false, null);
    GameFeatures.MotifOccurrence occ2 =
        new GameFeatures.MotifOccurrence(
            12, 6, "black", "Discovered check", "Nd5f4", "Ba2", "kf7", false, false, null);
    Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences =
        Map.of(
            Motif.PIN, List.of(occ1),
            Motif.DISCOVERED_CHECK, List.of(occ2));

    dao.insertOccurrences(gameUrl, occurrences);

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));

    assertThat(result).containsKey(gameUrl);
    Map<String, List<OccurrenceRow>> byMotif = result.get(gameUrl);
    assertThat(byMotif).containsKey("pin");
    assertThat(byMotif.get("pin"))
        .containsExactly(
            new OccurrenceRow(
                gameUrl,
                "pin",
                3,
                "white",
                "Knight pinned on c6",
                null,
                null,
                null,
                false,
                false,
                null));
    assertThat(byMotif).containsKey("discovered_check");
    assertThat(byMotif.get("discovered_check"))
        .containsExactly(
            new OccurrenceRow(
                gameUrl,
                "discovered_check",
                6,
                "black",
                "Discovered check",
                "Nd5f4",
                "Ba2",
                "kf7",
                false,
                false,
                null));
  }

  @Test
  public void queryOccurrences_emptyList_returnsEmptyMap() {
    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of());
    assertThat(result).isEmpty();
  }

  @Test
  public void queryOccurrences_unknownGameUrl_returnsNoOccurrences() {
    String gameUrl = "https://chess.com/game/nonexistent";
    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    // DAO only adds keys for game_urls that have rows; unknown URL has no rows
    assertThat(result.getOrDefault(gameUrl, Map.of())).isEmpty();
  }

  @Test
  public void insertOccurrences_skipsPlyZeroOccurrences() {
    String gameUrl = "https://chess.com/game/ply-zero";
    GameFeature game = createGame(gameUrl);
    dao.insert(game);

    GameFeatures.MotifOccurrence atPlyZero =
        new GameFeatures.MotifOccurrence(
            0, 0, "white", "initial", null, null, null, false, false, null);
    Map<Motif, List<GameFeatures.MotifOccurrence>> onlyPlyZero =
        Map.of(Motif.CHECK, List.of(atPlyZero));

    dao.insertOccurrences(gameUrl, onlyPlyZero);

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    // No rows inserted (ply 0 skipped), so no occurrences for this game
    assertThat(result.getOrDefault(gameUrl, Map.of())).isEmpty();
  }

  @Test
  public void insertOccurrences_isDiscovered_and_isMate_roundTrip() {
    String gameUrl = "https://chess.com/game/attack-1";
    GameFeature game = createGame(gameUrl);
    dao.insert(game);

    GameFeatures.MotifOccurrence discovered =
        new GameFeatures.MotifOccurrence(
            5, 3, "white", "Discovered attack at move 3", "Kg1g2", "Ra1", "rh1", true, false, null);
    GameFeatures.MotifOccurrence mate =
        new GameFeatures.MotifOccurrence(
            7, 4, "white", "Attack at move 4", "Ra1a5", "Ra5", "ka8", false, true, null);
    Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences =
        Map.of(Motif.ATTACK, List.of(discovered, mate));
    dao.insertOccurrences(gameUrl, occurrences);

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    assertThat(result).containsKey(gameUrl);
    List<OccurrenceRow> rows = result.get(gameUrl).get("attack");
    assertThat(rows).hasSize(2);
    assertThat(rows.get(0))
        .isEqualTo(
            new OccurrenceRow(
                gameUrl,
                "attack",
                3,
                "white",
                "Discovered attack at move 3",
                "Kg1g2",
                "Ra1",
                "rh1",
                true,
                false,
                null));
    assertThat(rows.get(1))
        .isEqualTo(
            new OccurrenceRow(
                gameUrl,
                "attack",
                4,
                "white",
                "Attack at move 4",
                "Ra1a5",
                "Ra5",
                "ka8",
                false,
                true,
                null));
  }

  @Test
  public void query_withCompiledQuery_returnsRowsAndRespectsLimit() {
    String url1 = "https://chess.com/game/q1";
    String url2 = "https://chess.com/game/q2";
    dao.insert(createGame(url1));
    dao.insert(createGame(url2));

    CompiledQuery compiled = new SqlCompiler().compile(Parser.parse("white_elo >= 1000"));
    List<GameFeature> rows = dao.query(compiled, 10, 0);

    assertThat(rows).hasSize(2);
    assertThat(rows.stream().map(GameFeature::gameUrl)).containsExactlyInAnyOrder(url1, url2);
  }

  // === fetchForReanalysis ===

  @Test
  public void fetchForReanalysis_emptyTable_returnsEmptyList() {
    List<GameForReanalysis> results = dao.fetchForReanalysis(10, 0);
    assertThat(results).isEmpty();
  }

  @Test
  public void fetchForReanalysis_returnsGameUrlAndPgn() {
    String gameUrl = "https://chess.com/game/reanalysis-1";
    dao.insert(createGame(gameUrl));

    List<GameForReanalysis> results = dao.fetchForReanalysis(10, 0);

    assertThat(results).hasSize(1);
    assertThat(results.get(0).gameUrl()).isEqualTo(gameUrl);
    assertThat(results.get(0).pgn()).isEqualTo("pgn");
    assertThat(results.get(0).requestId()).isEqualTo(requestId);
  }

  @Test
  public void fetchForReanalysis_respectsLimitAndOffset() {
    dao.insert(createGame("https://chess.com/game/r1"));
    dao.insert(createGame("https://chess.com/game/r2"));
    dao.insert(createGame("https://chess.com/game/r3"));

    List<GameForReanalysis> firstTwo = dao.fetchForReanalysis(2, 0);
    List<GameForReanalysis> lastOne = dao.fetchForReanalysis(2, 2);

    assertThat(firstTwo).hasSize(2);
    assertThat(lastOne).hasSize(1);

    List<String> allUrls = new java.util.ArrayList<>();
    firstTwo.stream().map(GameForReanalysis::gameUrl).forEach(allUrls::add);
    lastOne.stream().map(GameForReanalysis::gameUrl).forEach(allUrls::add);
    assertThat(allUrls)
        .containsExactlyInAnyOrder(
            "https://chess.com/game/r1", "https://chess.com/game/r2", "https://chess.com/game/r3");
  }

  @Test
  public void fetchForReanalysis_offsetBeyondEnd_returnsEmptyList() {
    dao.insert(createGame("https://chess.com/game/r1"));

    List<GameForReanalysis> results = dao.fetchForReanalysis(10, 5);

    assertThat(results).isEmpty();
  }

  // === updateMotifs ===

  @Test
  public void updateMotifs_updatesMotifColumnsForMatchingGame() {
    String gameUrl = "https://chess.com/game/motif-update-1";
    dao.insert(createGame(gameUrl)); // all motif columns initially false

    // Verify initially not returned by a pin query
    CompiledQuery pinQuery = new SqlCompiler().compile(Parser.parse("motif(pin)"));
    assertThat(dao.query(pinQuery, 10, 0)).isEmpty();

    GameFeatures features =
        new GameFeatures(
            Set.of(Motif.PIN, Motif.CHECK),
            20,
            Map.of(
                Motif.PIN,
                List.of(
                    new GameFeatures.MotifOccurrence(
                        7, 4, "white", "Pin", null, "Bb5", "nc6", false, false, "ABSOLUTE")),
                Motif.CHECK,
                List.of(
                    new GameFeatures.MotifOccurrence(
                        7, 4, "white", "Check", null, "Bb5", "ke8", false, false, null))));

    dao.updateMotifs(gameUrl, features);

    assertThat(dao.query(pinQuery, 10, 0)).hasSize(1);
    CompiledQuery checkQuery = new SqlCompiler().compile(Parser.parse("motif(check)"));
    assertThat(dao.query(checkQuery, 10, 0)).hasSize(1);
    CompiledQuery forkQuery = new SqlCompiler().compile(Parser.parse("motif(fork)"));
    assertThat(dao.query(forkQuery, 10, 0)).isEmpty();
  }

  @Test
  public void updateMotifs_doesNotAffectOtherGames() {
    String url1 = "https://chess.com/game/motif-update-2a";
    String url2 = "https://chess.com/game/motif-update-2b";
    dao.insert(createGame(url1));
    dao.insert(createGame(url2));

    GameFeatures features =
        new GameFeatures(
            Set.of(Motif.PIN),
            10,
            Map.of(
                Motif.PIN,
                List.of(
                    new GameFeatures.MotifOccurrence(
                        3, 2, "white", "Pin", null, "Bb5", "nc6", false, false, "ABSOLUTE"))));
    dao.updateMotifs(url1, features);

    CompiledQuery pinQuery = new SqlCompiler().compile(Parser.parse("motif(pin)"));
    List<GameFeature> pinned = dao.query(pinQuery, 10, 0);
    assertThat(pinned).hasSize(1);
    assertThat(pinned.get(0).gameUrl()).isEqualTo(url1);
  }

  @Test
  public void updateMotifs_derivesHasDiscoveredAttackFromAttackOccurrences() throws Exception {
    String gameUrl = "https://chess.com/game/motif-update-3";
    dao.insert(createGame(gameUrl));

    // ATTACK occurrence with isDiscovered=true → has_discovered_attack should be set to true
    GameFeatures.MotifOccurrence discoveredAttack =
        new GameFeatures.MotifOccurrence(
            9, 5, "black", "Discovered attack", "nc6d4", "bd7", "Bb5", true, false, null);
    GameFeatures features =
        new GameFeatures(Set.of(Motif.ATTACK), 10, Map.of(Motif.ATTACK, List.of(discoveredAttack)));

    dao.updateMotifs(gameUrl, features);

    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps =
            conn.prepareStatement(
                "SELECT has_discovered_attack FROM game_features WHERE game_url = ?")) {
      ps.setString(1, gameUrl);
      ResultSet rs = ps.executeQuery();
      assertThat(rs.next()).isTrue();
      assertThat(rs.getBoolean("has_discovered_attack")).isTrue();
    }
  }

  @Test
  public void updateMotifs_derivesHasCheckmateFromAttackOccurrencesWithIsMate() throws Exception {
    String gameUrl = "https://chess.com/game/motif-update-4";
    dao.insert(createGame(gameUrl));

    // ATTACK occurrence with isMate=true → has_checkmate should be set to true
    GameFeatures.MotifOccurrence mateAttack =
        new GameFeatures.MotifOccurrence(
            107, 54, "white", "Checkmate", "Ra1a5", "Ra5", "ka8", false, true, null);
    GameFeatures features =
        new GameFeatures(Set.of(Motif.ATTACK), 54, Map.of(Motif.ATTACK, List.of(mateAttack)));

    dao.updateMotifs(gameUrl, features);

    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps =
            conn.prepareStatement("SELECT has_checkmate FROM game_features WHERE game_url = ?")) {
      ps.setString(1, gameUrl);
      ResultSet rs = ps.executeQuery();
      assertThat(rs.next()).isTrue();
      assertThat(rs.getBoolean("has_checkmate")).isTrue();
    }
  }

  @Test
  public void updateMotifs_derivesHasDiscoveredMateFromAttackOccurrencesWithBothFlags()
      throws Exception {
    String gameUrl = "https://chess.com/game/motif-update-5";
    dao.insert(createGame(gameUrl));

    // ATTACK occurrence with isDiscovered=true AND isMate=true → has_discovered_mate=true
    GameFeatures.MotifOccurrence discoveredMate =
        new GameFeatures.MotifOccurrence(
            9, 5, "white", "Discovered mate", "Pe2e4", "Ra1", "ke8", true, true, null);
    GameFeatures features =
        new GameFeatures(Set.of(Motif.ATTACK), 10, Map.of(Motif.ATTACK, List.of(discoveredMate)));

    dao.updateMotifs(gameUrl, features);

    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps =
            conn.prepareStatement(
                "SELECT has_discovered_mate, has_checkmate FROM game_features WHERE game_url ="
                    + " ?")) {
      ps.setString(1, gameUrl);
      ResultSet rs = ps.executeQuery();
      assertThat(rs.next()).isTrue();
      assertThat(rs.getBoolean("has_discovered_mate")).isTrue();
      assertThat(rs.getBoolean("has_checkmate")).isTrue();
    }
  }

  @Test
  public void updateMotifs_noDiscoveredAttack_whenAllAttacksAreDirect() throws Exception {
    String gameUrl = "https://chess.com/game/motif-update-6";
    dao.insert(createGame(gameUrl));

    // ATTACK occurrence with isDiscovered=false → has_discovered_attack stays false
    GameFeatures.MotifOccurrence directAttack =
        new GameFeatures.MotifOccurrence(
            5, 3, "white", "Direct attack", "Ra1a8", "Ra8", "ke8", false, false, null);
    GameFeatures features =
        new GameFeatures(Set.of(Motif.ATTACK), 5, Map.of(Motif.ATTACK, List.of(directAttack)));

    dao.updateMotifs(gameUrl, features);

    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps =
            conn.prepareStatement(
                "SELECT has_discovered_attack FROM game_features WHERE game_url = ?")) {
      ps.setString(1, gameUrl);
      ResultSet rs = ps.executeQuery();
      assertThat(rs.next()).isTrue();
      assertThat(rs.getBoolean("has_discovered_attack")).isFalse();
    }
  }

  private GameFeature createGame(String url) {
    return new GameFeature(
        null,
        requestId,
        url,
        "CHESS_COM",
        "w",
        "b",
        1500,
        1500,
        "blitz",
        "B00",
        "1-0",
        Instant.now(),
        20,
        false, // hasPin
        false, // hasCrossPin
        false, // hasFork
        false, // hasSkewer
        false, // hasDiscoveredAttack
        false, // hasDiscoveredMate
        false, // hasDiscoveredCheck
        false, // hasCheck
        false, // hasCheckmate
        false, // hasPromotion
        false, // hasPromotionWithCheck
        false, // hasPromotionWithCheckmate
        false, // hasBackRankMate
        false, // hasSmotheredMate
        false, // hasSacrifice
        false, // hasZugzwang
        false, // hasDoubleCheck

        false, // hasOverloadedPiece
        Instant.now(),
        "pgn");
  }
}
