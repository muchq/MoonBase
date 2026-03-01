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
import java.time.Instant;
import java.util.List;
import java.util.Map;
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
    // Discovered attack targeting king — derived as both discovered_attack and discovered_check.
    GameFeatures.MotifOccurrence occ2 =
        GameFeatures.MotifOccurrence.attack(
            12, 6, "black", "Discovered attack at move 6", "Nd5f4", "Ba2", "kf7", true, false);
    Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences =
        Map.of(
            Motif.PIN, List.of(occ1),
            Motif.ATTACK, List.of(occ2));

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
                "Discovered check at move 6",
                "Nd5f4",
                "Ba2",
                "kf7",
                true,
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
  public void attack_notExposedInQueryOccurrences() {
    // ATTACK is an internal backend primitive and must not appear in queryOccurrences results.
    // It is stored (for ChessQL derived-motif queries) but filtered before returning to callers.
    String gameUrl = "https://chess.com/game/attack-1";
    dao.insert(createGame(gameUrl));

    GameFeatures.MotifOccurrence discovered =
        new GameFeatures.MotifOccurrence(
            5, 3, "white", "Discovered attack at move 3", "Kg1g2", "Ra1", "rh1", true, false, null);
    GameFeatures.MotifOccurrence mate =
        new GameFeatures.MotifOccurrence(
            7, 4, "white", "Attack at move 4", "Ra1a5", "Ra5", "ka8", false, true, null);
    dao.insertOccurrences(gameUrl, Map.of(Motif.ATTACK, List.of(discovered, mate)));

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    Map<String, List<OccurrenceRow>> byMotif = result.getOrDefault(gameUrl, Map.of());
    assertThat(byMotif).doesNotContainKey("attack");
  }

  @Test
  public void fork_derivedFromAttackRowsInQueryOccurrences() {
    // Two ATTACK rows at the same (moveNumber, side, attacker) with different targets = fork.
    String gameUrl = "https://chess.com/game/fork-1";
    dao.insert(createGame(gameUrl));

    // Ng6 at move 8 attacks both rh6 and ke8 — this is a fork
    GameFeatures.MotifOccurrence attack1 =
        GameFeatures.MotifOccurrence.attack(
            15, 8, "white", "Attack at move 8", "Ng5g6", "Ng6", "rh6", false, false);
    GameFeatures.MotifOccurrence attack2 =
        GameFeatures.MotifOccurrence.attack(
            15, 8, "white", "Attack at move 8", "Ng5g6", "Ng6", "ke8", false, false);
    dao.insertOccurrences(gameUrl, Map.of(Motif.ATTACK, List.of(attack1, attack2)));

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    Map<String, List<OccurrenceRow>> byMotif = result.get(gameUrl);
    assertThat(byMotif).doesNotContainKey("attack");
    assertThat(byMotif).containsKey("fork");
    List<OccurrenceRow> forkOccs = byMotif.get("fork");
    assertThat(forkOccs).hasSize(2);
    assertThat(forkOccs).allMatch(o -> o.moveNumber() == 8);
    assertThat(forkOccs).allMatch(o -> "white".equals(o.side()));
    assertThat(forkOccs).allMatch(o -> "Ng6".equals(o.attacker()));
    assertThat(forkOccs).extracting(OccurrenceRow::target).containsExactlyInAnyOrder("rh6", "ke8");
  }

  @Test
  public void fork_notDerivedWhenSingleTarget() {
    // One ATTACK row per attacker — not a fork.
    String gameUrl = "https://chess.com/game/no-fork-1";
    dao.insert(createGame(gameUrl));

    GameFeatures.MotifOccurrence attack =
        GameFeatures.MotifOccurrence.attack(
            15, 8, "white", "Attack at move 8", "Ng5g6", "Ng6", "ke8", false, false);
    dao.insertOccurrences(gameUrl, Map.of(Motif.ATTACK, List.of(attack)));

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    Map<String, List<OccurrenceRow>> byMotif = result.getOrDefault(gameUrl, Map.of());
    assertThat(byMotif).doesNotContainKey("fork");
    assertThat(byMotif).doesNotContainKey("attack");
  }

  @Test
  public void fork_notDerivedFromDiscoveredAttacks() {
    // Discovered attacks (isDiscovered=true) must not count toward fork grouping.
    String gameUrl = "https://chess.com/game/no-fork-discovered";
    dao.insert(createGame(gameUrl));

    GameFeatures.MotifOccurrence disc1 =
        GameFeatures.MotifOccurrence.attack(
            15, 8, "white", "Discovered", "Pf5", "Bg2", "rh6", true, false);
    GameFeatures.MotifOccurrence disc2 =
        GameFeatures.MotifOccurrence.attack(
            15, 8, "white", "Discovered", "Pf5", "Bg2", "ke8", true, false);
    dao.insertOccurrences(gameUrl, Map.of(Motif.ATTACK, List.of(disc1, disc2)));

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    assertThat(result.getOrDefault(gameUrl, Map.of())).doesNotContainKey("fork");
  }

  @Test
  public void discoveredAttack_derivedFromIsDiscoveredAttackRows() {
    String gameUrl = "https://chess.com/game/disc-attack-1";
    dao.insert(createGame(gameUrl));

    // Discovered attack: Kg1g2 reveals Ra1 attacking rh1
    GameFeatures.MotifOccurrence disc =
        GameFeatures.MotifOccurrence.attack(
            59, 30, "white", "Discovered attack at move 30", "Kg1g2", "Ra1", "rh1", true, false);
    // Direct attack — not discovered
    GameFeatures.MotifOccurrence direct =
        GameFeatures.MotifOccurrence.attack(
            59, 30, "white", "Attack at move 30", "Kg1g2", "Kg2", "qe5", false, false);
    dao.insertOccurrences(gameUrl, Map.of(Motif.ATTACK, List.of(disc, direct)));

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    Map<String, List<OccurrenceRow>> byMotif = result.get(gameUrl);
    assertThat(byMotif).containsKey("discovered_attack");
    assertThat(byMotif).doesNotContainKey("attack");
    List<OccurrenceRow> occs = byMotif.get("discovered_attack");
    assertThat(occs).hasSize(1);
    assertThat(occs.get(0).moveNumber()).isEqualTo(30);
    assertThat(occs.get(0).side()).isEqualTo("white");
    assertThat(occs.get(0).attacker()).isEqualTo("Ra1");
    assertThat(occs.get(0).target()).isEqualTo("rh1");
    assertThat(occs.get(0).isDiscovered()).isTrue();
  }

  @Test
  public void checkmate_derivedFromIsMateAttackRows() {
    String gameUrl = "https://chess.com/game/checkmate-1";
    dao.insert(createGame(gameUrl));

    // Ra5 delivers checkmate to ka8 at move 54
    GameFeatures.MotifOccurrence mateAttack =
        GameFeatures.MotifOccurrence.attack(
            107, 54, "white", "Attack at move 54", "Ra5", "Ra5", "ka8", false, true);
    dao.insertOccurrences(gameUrl, Map.of(Motif.ATTACK, List.of(mateAttack)));

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    Map<String, List<OccurrenceRow>> byMotif = result.get(gameUrl);
    assertThat(byMotif).containsKey("checkmate");
    assertThat(byMotif).doesNotContainKey("attack");
    List<OccurrenceRow> occs = byMotif.get("checkmate");
    assertThat(occs).hasSize(1);
    assertThat(occs.get(0).moveNumber()).isEqualTo(54);
    assertThat(occs.get(0).side()).isEqualTo("white");
    assertThat(occs.get(0).attacker()).isEqualTo("Ra5");
    assertThat(occs.get(0).target()).isEqualTo("ka8");
    assertThat(occs.get(0).isMate()).isTrue();
  }

  @Test
  public void discoveredCheck_derivedFromDiscoveredAttackTargetingKing() {
    String gameUrl = "https://chess.com/game/disc-check-1";
    dao.insert(createGame(gameUrl));

    // Discovered check: Pf5 moves revealing Bg2 attacking ke8
    GameFeatures.MotifOccurrence discCheck =
        GameFeatures.MotifOccurrence.attack(
            15, 8, "white", "Discovered attack at move 8", "Pf5", "Bg2", "ke8", true, false);
    // Discovered attack targeting non-king — must NOT become discovered_check
    GameFeatures.MotifOccurrence discNonKing =
        GameFeatures.MotifOccurrence.attack(
            15, 8, "white", "Discovered attack at move 8", "Pf5", "Bg2", "qd5", true, false);
    dao.insertOccurrences(gameUrl, Map.of(Motif.ATTACK, List.of(discCheck, discNonKing)));

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    Map<String, List<OccurrenceRow>> byMotif = result.get(gameUrl);
    assertThat(byMotif).containsKey("discovered_check");
    assertThat(byMotif).containsKey("discovered_attack");
    // Only the king-targeting row becomes discovered_check
    List<OccurrenceRow> occs = byMotif.get("discovered_check");
    assertThat(occs).hasSize(1);
    assertThat(occs.get(0).attacker()).isEqualTo("Bg2");
    assertThat(occs.get(0).target()).isEqualTo("ke8");
    assertThat(occs.get(0).isDiscovered()).isTrue();
  }

  @Test
  public void doubleCheck_derivedWhenTwoAttackersTargetKingAtSamePly() {
    String gameUrl = "https://chess.com/game/double-check-1";
    dao.insert(createGame(gameUrl));

    // Move 10: piece moves delivering check (direct) AND reveals discovered check — double check
    GameFeatures.MotifOccurrence direct =
        GameFeatures.MotifOccurrence.attack(
            19, 10, "white", "Attack at move 10", "Bd3", "Bd3", "ke8", false, false);
    GameFeatures.MotifOccurrence discovered =
        GameFeatures.MotifOccurrence.attack(
            19, 10, "white", "Discovered attack at move 10", "Bd3", "Rd1", "ke8", true, false);
    dao.insertOccurrences(gameUrl, Map.of(Motif.ATTACK, List.of(direct, discovered)));

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    Map<String, List<OccurrenceRow>> byMotif = result.get(gameUrl);
    assertThat(byMotif).containsKey("double_check");
    List<OccurrenceRow> occs = byMotif.get("double_check");
    assertThat(occs).hasSize(1);
    assertThat(occs.get(0).moveNumber()).isEqualTo(10);
    assertThat(occs.get(0).side()).isEqualTo("white");
    assertThat(occs.get(0).target()).isEqualTo("ke8");
  }

  @Test
  public void doubleCheck_notDerivedWhenSingleAttackerTargetsKing() {
    String gameUrl = "https://chess.com/game/no-double-check-1";
    dao.insert(createGame(gameUrl));

    GameFeatures.MotifOccurrence single =
        GameFeatures.MotifOccurrence.attack(
            19, 10, "white", "Attack at move 10", "Bd3", "Bd3", "ke8", false, false);
    dao.insertOccurrences(gameUrl, Map.of(Motif.ATTACK, List.of(single)));

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    assertThat(result.getOrDefault(gameUrl, Map.of())).doesNotContainKey("double_check");
  }

  @Test
  public void staleStoredMotifs_filteredFromResults() {
    // Stale CHECKMATE, DISCOVERED_CHECK, DOUBLE_CHECK, DISCOVERED_ATTACK rows from old index runs
    // must be excluded from queryOccurrences (filtered in SQL) and re-derived from ATTACK rows.
    String gameUrl = "https://chess.com/game/stale-derived-1";
    dao.insert(createGame(gameUrl));

    // Insert stale stored rows directly (simulating old indexed data)
    try (var conn = dataSource.getConnection()) {
      for (String staleMotif :
          List.of("CHECKMATE", "DISCOVERED_CHECK", "DOUBLE_CHECK", "DISCOVERED_ATTACK", "FORK")) {
        try (var ps =
            conn.prepareStatement(
                "INSERT INTO motif_occurrences (id, game_url, motif, ply, side, move_number,"
                    + " description, moved_piece, attacker, target, is_discovered, is_mate,"
                    + " pin_type) VALUES (?, ?, ?, 5, 'white', 3, 'stale', null, null, null,"
                    + " false, false, null)")) {
          ps.setString(1, java.util.UUID.randomUUID().toString());
          ps.setString(2, gameUrl);
          ps.setString(3, staleMotif);
          ps.executeUpdate();
        }
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    // No ATTACK rows → no derived motifs; stale stored rows are filtered out
    assertThat(result.getOrDefault(gameUrl, Map.of())).isEmpty();
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

  // === insertOccurrences and motif queries ===

  @Test
  public void insertOccurrences_enablesMotifQuery() {
    String gameUrl = "https://chess.com/game/motif-query-1";
    dao.insert(createGame(gameUrl));

    CompiledQuery pinQuery = new SqlCompiler().compile(Parser.parse("motif(pin)"));
    assertThat(dao.query(pinQuery, 10, 0)).isEmpty();

    Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences =
        Map.of(
            Motif.PIN,
            List.of(
                new GameFeatures.MotifOccurrence(
                    7, 4, "white", "Pin", null, "Bb5", "nc6", false, false, "ABSOLUTE")),
            Motif.CHECK,
            List.of(
                new GameFeatures.MotifOccurrence(
                    7, 4, "white", "Check", null, "Bb5", "ke8", false, false, null)));

    dao.insertOccurrences(gameUrl, occurrences);

    assertThat(dao.query(pinQuery, 10, 0)).hasSize(1);
    CompiledQuery checkQuery = new SqlCompiler().compile(Parser.parse("motif(check)"));
    assertThat(dao.query(checkQuery, 10, 0)).hasSize(1);
    CompiledQuery forkQuery = new SqlCompiler().compile(Parser.parse("motif(fork)"));
    assertThat(dao.query(forkQuery, 10, 0)).isEmpty();
  }

  @Test
  public void insertOccurrences_doesNotAffectOtherGames() {
    String url1 = "https://chess.com/game/motif-isolation-1";
    String url2 = "https://chess.com/game/motif-isolation-2";
    dao.insert(createGame(url1));
    dao.insert(createGame(url2));

    Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences =
        Map.of(
            Motif.PIN,
            List.of(
                new GameFeatures.MotifOccurrence(
                    3, 2, "white", "Pin", null, "Bb5", "nc6", false, false, "ABSOLUTE")));
    dao.insertOccurrences(url1, occurrences);

    CompiledQuery pinQuery = new SqlCompiler().compile(Parser.parse("motif(pin)"));
    List<GameFeature> pinned = dao.query(pinQuery, 10, 0);
    assertThat(pinned).hasSize(1);
    assertThat(pinned.get(0).gameUrl()).isEqualTo(url1);
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
        Instant.now(),
        "pgn");
  }
}
