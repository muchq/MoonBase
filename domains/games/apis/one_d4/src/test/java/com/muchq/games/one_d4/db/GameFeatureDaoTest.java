package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.ParsedQuery;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.dto.AggregateRow;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.db.GameFeatureStore.GameForReanalysis;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.time.Clock;
import java.time.Instant;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class GameFeatureDaoTest {

  private TestDb testDb;
  private GameFeatureDao dao;
  private UUID requestId;

  @BeforeEach
  public void setUp() {
    testDb = TestDb.create("gamefeaturedao");
    dao = new GameFeatureDao(testDb.jdbi(), true);
    requestId = UUID.randomUUID();

    try (var conn = testDb.dataSource().getConnection();
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
  public void insertBatch_insertsMultipleGames() {
    String url1 = "https://chess.com/game/batch-1";
    String url2 = "https://chess.com/game/batch-2";
    String url3 = "https://chess.com/game/batch-3";
    dao.insertBatch(List.of(createGame(url1), createGame(url2), createGame(url3)));

    CompiledQuery allGames = new SqlCompiler().compile(Parser.parse("white_elo >= 1000"));
    List<GameFeature> rows = dao.query(allGames, 10, 0);
    assertThat(rows).hasSize(3);
    assertThat(rows.stream().map(GameFeature::gameUrl)).containsExactlyInAnyOrder(url1, url2, url3);
  }

  @Test
  public void insertBatch_emptyList_noOp() {
    dao.insertBatch(List.of());
    CompiledQuery allGames = new SqlCompiler().compile(Parser.parse("white_elo >= 1000"));
    assertThat(dao.query(allGames, 10, 0)).isEmpty();
  }

  /** H2 alias target for the slow-query tests: sleeps per evaluated row. Must be public static. */
  public static int nap(int millis) throws InterruptedException {
    Thread.sleep(millis);
    return 0;
  }

  /**
   * The read-path timeout must cancel a query that is actually running, not merely be set. 400 rows
   * sleeping 20ms each is a ~8s query; a 1s timeout has to kill it partway. H2 enforces the timeout
   * cooperatively and only checks every so many processed rows, which is why the fixture needs
   * hundreds of rows rather than a handful — with too few rows the check never runs and the query
   * completes as if unbounded. The positive twin below shares the fixture (same alias, same query
   * shape), so a broken alias cannot masquerade as the timeout firing.
   */
  @Test
  public void query_slowExecutionIsCancelledByTheReadTimeout() throws Exception {
    try (var conn = testDb.dataSource().getConnection();
        var stmt = conn.createStatement()) {
      stmt.execute(
          "CREATE ALIAS IF NOT EXISTS NAP FOR"
              + " \"com.muchq.games.one_d4.db.GameFeatureDaoTest.nap\"");
    }
    List<GameFeature> games = new ArrayList<>();
    for (int i = 0; i < 400; i++) {
      games.add(createGame("https://chess.com/game/slow-" + i));
    }
    dao.insertBatch(games);

    GameFeatureDao boundedDao = new GameFeatureDao(testDb.jdbi(), true, Clock.systemUTC(), 1);
    CompiledQuery slow =
        new CompiledQuery(
            "SELECT g.* FROM game_features g WHERE NAP(20) = 0"
                + " ORDER BY g.played_at DESC, g.game_url ASC",
            List.of());

    long start = System.nanoTime();
    org.assertj.core.api.Assertions.assertThatThrownBy(() -> boundedDao.query(slow, 500, 0))
        .as("a query outrunning the timeout must be cancelled, not awaited")
        .isInstanceOf(Exception.class);
    long elapsedMillis = (System.nanoTime() - start) / 1_000_000;
    assertThat(elapsedMillis)
        .as("cancellation must arrive well before the ~8s the full query needs")
        .isLessThan(7_000);

    // Positive twin: the same query shape under a negligible per-row sleep completes normally,
    // proving the alias and the crafted CompiledQuery work and the failure above is the timeout.
    CompiledQuery fast =
        new CompiledQuery(
            "SELECT g.* FROM game_features g WHERE NAP(0) = 0"
                + " ORDER BY g.played_at DESC, g.game_url ASC",
            List.of());
    assertThat(boundedDao.query(fast, 500, 0)).hasSize(400);
  }

  /**
   * The production constructors must apply the 10-second timeout to all four read statements —
   * observed on the real JDBC statements via Jdbi's logger, per "test through the same objects
   * production uses". Asserted against the literal 10 rather than the constant, so mutating the
   * constant cannot mutate the expectation with it. Any read path losing its timeout regresses to
   * unbounded execution silently; this is what notices.
   *
   * <p>The final probe pins the H2 session cleanup: H2 scopes setQueryTimeout to the pooled
   * connection's session, so without the DAO's reset a read's timeout would leak into every later
   * statement on that connection — including writes, which are deliberately unbounded. It also
   * keeps the per-path probes honest: with a leaked session value, a read path that forgot its own
   * setQueryTimeout would inherit an earlier read's and still probe as 10.
   */
  @Test
  public void allFourReadPathsCarryTheProductionTimeout() {
    java.util.concurrent.atomic.AtomicInteger lastTimeout =
        new java.util.concurrent.atomic.AtomicInteger(-1);
    testDb
        .jdbi()
        .setSqlLogger(
            new org.jdbi.v3.core.statement.SqlLogger() {
              @Override
              public void logBeforeExecution(org.jdbi.v3.core.statement.StatementContext ctx) {
                try {
                  // Batch statements log with a null statement here; only reads are probed.
                  var statement = ctx.getStatement();
                  if (statement != null) {
                    lastTimeout.set(statement.getQueryTimeout());
                  }
                } catch (java.sql.SQLException e) {
                  throw new RuntimeException(e);
                }
              }
            });
    GameFeatureDao prodDao = new GameFeatureDao(testDb.jdbi(), true);
    dao.insertBatch(List.of(createGame("https://chess.com/game/timeout-probe")));

    lastTimeout.set(-1);
    prodDao.query(new SqlCompiler().compile(Parser.parse("white_elo >= 1000")), 10, 0);
    assertThat(lastTimeout.get()).as("query()").isEqualTo(10);

    lastTimeout.set(-1);
    prodDao.queryOccurrences(List.of("https://chess.com/game/timeout-probe"));
    assertThat(lastTimeout.get()).as("queryOccurrences()").isEqualTo(10);

    lastTimeout.set(-1);
    prodDao.aggregate(
        new CompiledQuery(
            "SELECT opening_family, COUNT(*) AS group_count FROM game_features"
                + " GROUP BY opening_family",
            List.of()),
        List.of("opening_family"),
        10);
    assertThat(lastTimeout.get()).as("aggregate()").isEqualTo(10);

    lastTimeout.set(-1);
    prodDao.aggregateTotals(
        new CompiledQuery(
            "SELECT COUNT(*) AS total_games, 1 AS total_groups FROM game_features", List.of()));
    assertThat(lastTimeout.get()).as("aggregateTotals()").isEqualTo(10);

    // An unrelated statement on the same (pooled) session must NOT inherit the read timeout.
    lastTimeout.set(-1);
    testDb.jdbi().withHandle(h -> h.createQuery("SELECT 1").mapTo(Integer.class).one());
    assertThat(lastTimeout.get())
        .as("the read timeout must not leak to later statements on the session")
        .isEqualTo(0);
  }

  @Test
  public void insertOccurrencesBatch_insertsAcrossMultipleGames() {
    String url1 = "https://chess.com/game/occ-batch-1";
    String url2 = "https://chess.com/game/occ-batch-2";
    dao.insertBatch(List.of(createGame(url1), createGame(url2)));

    GameFeatures.MotifOccurrence pin =
        new GameFeatures.MotifOccurrence(
            5, 3, "white", "Pin on c6", null, "Bb5", "nc6", false, false, "ABSOLUTE");
    GameFeatures.MotifOccurrence check =
        new GameFeatures.MotifOccurrence(
            7, 4, "black", "Check at move 4", null, "Qd8", "Ke1", false, false, null);

    Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> batch = new LinkedHashMap<>();
    batch.put(url1, Map.of(Motif.PIN, List.of(pin)));
    batch.put(url2, Map.of(Motif.CHECK, List.of(check)));
    dao.insertOccurrencesBatch(batch);

    Map<String, Map<String, List<OccurrenceRow>>> result =
        dao.queryOccurrences(List.of(url1, url2));
    assertThat(result).containsKey(url1);
    assertThat(result.get(url1)).containsKey("pin");
    assertThat(result).containsKey(url2);
    assertThat(result.get(url2)).containsKey("check");
  }

  @Test
  public void insertOccurrencesBatch_emptyMap_noOp() {
    dao.insertOccurrencesBatch(Map.of());
    // No exception thrown, no rows inserted
  }

  @Test
  public void insertOccurrences_and_queryOccurrences_roundTrip() {
    String gameUrl = "https://chess.com/game/occ-1";
    GameFeature game = createGame(gameUrl);
    dao.insertBatch(List.of(game));

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

    dao.insertOccurrencesBatch(Map.of(gameUrl, occurrences));

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
    dao.insertBatch(List.of(game));

    GameFeatures.MotifOccurrence atPlyZero =
        new GameFeatures.MotifOccurrence(
            0, 0, "white", "initial", null, null, null, false, false, null);
    Map<Motif, List<GameFeatures.MotifOccurrence>> onlyPlyZero =
        Map.of(Motif.CHECK, List.of(atPlyZero));

    dao.insertOccurrencesBatch(Map.of(gameUrl, onlyPlyZero));

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    // No rows inserted (ply 0 skipped), so no occurrences for this game
    assertThat(result.getOrDefault(gameUrl, Map.of())).isEmpty();
  }

  @Test
  public void attack_notExposedInQueryOccurrences() {
    // ATTACK is an internal backend primitive and must not appear in queryOccurrences results.
    // It is stored (for ChessQL derived-motif queries) but filtered before returning to callers.
    String gameUrl = "https://chess.com/game/attack-1";
    dao.insertBatch(List.of(createGame(gameUrl)));

    GameFeatures.MotifOccurrence discovered =
        new GameFeatures.MotifOccurrence(
            5, 3, "white", "Discovered attack at move 3", "Kg1g2", "Ra1", "rh1", true, false, null);
    GameFeatures.MotifOccurrence mate =
        new GameFeatures.MotifOccurrence(
            7, 4, "white", "Attack at move 4", "Ra1a5", "Ra5", "ka8", false, true, null);
    dao.insertOccurrencesBatch(Map.of(gameUrl, Map.of(Motif.ATTACK, List.of(discovered, mate))));

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    Map<String, List<OccurrenceRow>> byMotif = result.getOrDefault(gameUrl, Map.of());
    assertThat(byMotif).doesNotContainKey("attack");
  }

  @Test
  public void fork_derivedFromAttackRowsInQueryOccurrences() {
    // Two ATTACK rows at the same (moveNumber, side, attacker) with different targets = fork.
    String gameUrl = "https://chess.com/game/fork-1";
    dao.insertBatch(List.of(createGame(gameUrl)));

    // Ng6 at move 8 attacks both rh6 and ke8 — this is a fork
    GameFeatures.MotifOccurrence attack1 =
        GameFeatures.MotifOccurrence.attack(
            15, 8, "white", "Attack at move 8", "Ng5g6", "Ng6", "rh6", false, false);
    GameFeatures.MotifOccurrence attack2 =
        GameFeatures.MotifOccurrence.attack(
            15, 8, "white", "Attack at move 8", "Ng5g6", "Ng6", "ke8", false, false);
    dao.insertOccurrencesBatch(Map.of(gameUrl, Map.of(Motif.ATTACK, List.of(attack1, attack2))));

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
    dao.insertBatch(List.of(createGame(gameUrl)));

    GameFeatures.MotifOccurrence attack =
        GameFeatures.MotifOccurrence.attack(
            15, 8, "white", "Attack at move 8", "Ng5g6", "Ng6", "ke8", false, false);
    dao.insertOccurrencesBatch(Map.of(gameUrl, Map.of(Motif.ATTACK, List.of(attack))));

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    Map<String, List<OccurrenceRow>> byMotif = result.getOrDefault(gameUrl, Map.of());
    assertThat(byMotif).doesNotContainKey("fork");
    assertThat(byMotif).doesNotContainKey("attack");
  }

  @Test
  public void fork_notDerivedFromDiscoveredAttacks() {
    // Discovered attacks (isDiscovered=true) must not count toward fork grouping.
    String gameUrl = "https://chess.com/game/no-fork-discovered";
    dao.insertBatch(List.of(createGame(gameUrl)));

    GameFeatures.MotifOccurrence disc1 =
        GameFeatures.MotifOccurrence.attack(
            15, 8, "white", "Discovered", "Pf5", "Bg2", "rh6", true, false);
    GameFeatures.MotifOccurrence disc2 =
        GameFeatures.MotifOccurrence.attack(
            15, 8, "white", "Discovered", "Pf5", "Bg2", "ke8", true, false);
    dao.insertOccurrencesBatch(Map.of(gameUrl, Map.of(Motif.ATTACK, List.of(disc1, disc2))));

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    assertThat(result.getOrDefault(gameUrl, Map.of())).doesNotContainKey("fork");
  }

  @Test
  public void discoveredAttack_derivedFromIsDiscoveredAttackRows() {
    String gameUrl = "https://chess.com/game/disc-attack-1";
    dao.insertBatch(List.of(createGame(gameUrl)));

    // Discovered attack: Kg1g2 reveals Ra1 attacking rh1
    GameFeatures.MotifOccurrence disc =
        GameFeatures.MotifOccurrence.attack(
            59, 30, "white", "Discovered attack at move 30", "Kg1g2", "Ra1", "rh1", true, false);
    // Direct attack — not discovered
    GameFeatures.MotifOccurrence direct =
        GameFeatures.MotifOccurrence.attack(
            59, 30, "white", "Attack at move 30", "Kg1g2", "Kg2", "qe5", false, false);
    dao.insertOccurrencesBatch(Map.of(gameUrl, Map.of(Motif.ATTACK, List.of(disc, direct))));

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
    dao.insertBatch(List.of(createGame(gameUrl)));

    // Ra5 delivers checkmate to ka8 at move 54
    GameFeatures.MotifOccurrence mateAttack =
        GameFeatures.MotifOccurrence.attack(
            107, 54, "white", "Attack at move 54", "Ra5", "Ra5", "ka8", false, true);
    dao.insertOccurrencesBatch(Map.of(gameUrl, Map.of(Motif.ATTACK, List.of(mateAttack))));

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
    dao.insertBatch(List.of(createGame(gameUrl)));

    // Discovered check: Pf5 moves revealing Bg2 attacking ke8
    GameFeatures.MotifOccurrence discCheck =
        GameFeatures.MotifOccurrence.attack(
            15, 8, "white", "Discovered attack at move 8", "Pf5", "Bg2", "ke8", true, false);
    // Discovered attack targeting non-king — must NOT become discovered_check
    GameFeatures.MotifOccurrence discNonKing =
        GameFeatures.MotifOccurrence.attack(
            15, 8, "white", "Discovered attack at move 8", "Pf5", "Bg2", "qd5", true, false);
    dao.insertOccurrencesBatch(
        Map.of(gameUrl, Map.of(Motif.ATTACK, List.of(discCheck, discNonKing))));

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
    dao.insertBatch(List.of(createGame(gameUrl)));

    // Move 10: piece moves delivering check (direct) AND reveals discovered check — double check
    GameFeatures.MotifOccurrence direct =
        GameFeatures.MotifOccurrence.attack(
            19, 10, "white", "Attack at move 10", "Bd3", "Bd3", "ke8", false, false);
    GameFeatures.MotifOccurrence discovered =
        GameFeatures.MotifOccurrence.attack(
            19, 10, "white", "Discovered attack at move 10", "Bd3", "Rd1", "ke8", true, false);
    dao.insertOccurrencesBatch(Map.of(gameUrl, Map.of(Motif.ATTACK, List.of(direct, discovered))));

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
    dao.insertBatch(List.of(createGame(gameUrl)));

    GameFeatures.MotifOccurrence single =
        GameFeatures.MotifOccurrence.attack(
            19, 10, "white", "Attack at move 10", "Bd3", "Bd3", "ke8", false, false);
    dao.insertOccurrencesBatch(Map.of(gameUrl, Map.of(Motif.ATTACK, List.of(single))));

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    assertThat(result.getOrDefault(gameUrl, Map.of())).doesNotContainKey("double_check");
  }

  @Test
  public void staleStoredMotifs_filteredFromResults() {
    // Stale CHECKMATE, DISCOVERED_CHECK, DOUBLE_CHECK, DISCOVERED_ATTACK rows from old index runs
    // must be excluded from queryOccurrences (filtered in SQL) and re-derived from ATTACK rows.
    String gameUrl = "https://chess.com/game/stale-derived-1";
    dao.insertBatch(List.of(createGame(gameUrl)));

    // Insert stale stored rows directly (simulating old indexed data)
    try (var conn = testDb.dataSource().getConnection()) {
      for (String staleMotif :
          List.of("CHECKMATE", "DISCOVERED_CHECK", "DOUBLE_CHECK", "DISCOVERED_ATTACK", "FORK")) {
        try (var ps =
            conn.prepareStatement(
                "INSERT INTO motif_occurrences (id, game_url, motif, ply, side, move_number,"
                    + " description, moved_piece, attacker, target, is_discovered, is_mate,"
                    + " pin_type) VALUES (?, ?, ?, 5, 'white', 3, 'stale', null, null, null,"
                    + " false, false, null)")) {
          ps.setString(1, UUID.randomUUID().toString());
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
    dao.insertBatch(List.of(createGame(url1), createGame(url2)));

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
    dao.insertBatch(List.of(createGame(gameUrl)));

    List<GameForReanalysis> results = dao.fetchForReanalysis(10, 0);

    assertThat(results).hasSize(1);
    assertThat(results.get(0).gameUrl()).isEqualTo(gameUrl);
    assertThat(results.get(0).pgn()).isEqualTo("pgn");
    assertThat(results.get(0).requestId()).isEqualTo(requestId);
  }

  @Test
  public void fetchForReanalysis_respectsLimitAndOffset() {
    dao.insertBatch(
        List.of(
            createGame("https://chess.com/game/r1"),
            createGame("https://chess.com/game/r2"),
            createGame("https://chess.com/game/r3")));

    List<GameForReanalysis> firstTwo = dao.fetchForReanalysis(2, 0);
    List<GameForReanalysis> lastOne = dao.fetchForReanalysis(2, 2);

    assertThat(firstTwo).hasSize(2);
    assertThat(lastOne).hasSize(1);

    List<String> allUrls = new ArrayList<>();
    firstTwo.stream().map(GameForReanalysis::gameUrl).forEach(allUrls::add);
    lastOne.stream().map(GameForReanalysis::gameUrl).forEach(allUrls::add);
    assertThat(allUrls)
        .containsExactlyInAnyOrder(
            "https://chess.com/game/r1", "https://chess.com/game/r2", "https://chess.com/game/r3");
  }

  @Test
  public void fetchForReanalysis_offsetBeyondEnd_returnsEmptyList() {
    dao.insertBatch(List.of(createGame("https://chess.com/game/r1")));

    List<GameForReanalysis> results = dao.fetchForReanalysis(10, 5);

    assertThat(results).isEmpty();
  }

  // === insertOccurrences and motif queries ===

  @Test
  public void insertOccurrences_enablesMotifQuery() {
    String gameUrl = "https://chess.com/game/motif-query-1";
    dao.insertBatch(List.of(createGame(gameUrl)));

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

    dao.insertOccurrencesBatch(Map.of(gameUrl, occurrences));

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
    dao.insertBatch(List.of(createGame(url1), createGame(url2)));

    Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences =
        Map.of(
            Motif.PIN,
            List.of(
                new GameFeatures.MotifOccurrence(
                    3, 2, "white", "Pin", null, "Bb5", "nc6", false, false, "ABSOLUTE")));
    dao.insertOccurrencesBatch(Map.of(url1, occurrences));

    CompiledQuery pinQuery = new SqlCompiler().compile(Parser.parse("motif(pin)"));
    List<GameFeature> pinned = dao.query(pinQuery, 10, 0);
    assertThat(pinned).hasSize(1);
    assertThat(pinned.get(0).gameUrl()).isEqualTo(url1);
  }

  @Test
  public void query_returnsGamesInStableDescendingPlayedAtOrder() {
    // Insert games with different played_at values
    Instant older = Instant.parse("2024-01-01T00:00:00Z");
    Instant newer = Instant.parse("2024-06-01T00:00:00Z");
    dao.insertBatch(
        List.of(
            createGameAt("https://chess.com/game/order-a", older),
            createGameAt("https://chess.com/game/order-b", newer)));

    CompiledQuery allGames = new SqlCompiler().compile(Parser.parse("white_elo >= 1000"));
    List<GameFeature> page1 = dao.query(allGames, 1, 0);
    List<GameFeature> page2 = dao.query(allGames, 1, 1);

    assertThat(page1).hasSize(1);
    assertThat(page2).hasSize(1);
    // Newer game comes first (DESC), older game is on page 2
    assertThat(page1.get(0).gameUrl()).isEqualTo("https://chess.com/game/order-b");
    assertThat(page2.get(0).gameUrl()).isEqualTo("https://chess.com/game/order-a");
  }

  @Test
  public void query_paginatesStablyWhenPlayedAtIsEqual() {
    // Insert two games with identical played_at; game_url tiebreaker determines order
    Instant sameTime = Instant.parse("2024-03-01T12:00:00Z");
    dao.insertBatch(
        List.of(
            createGameAt("https://chess.com/game/zzz-last", sameTime),
            createGameAt("https://chess.com/game/aaa-first", sameTime)));

    CompiledQuery allGames = new SqlCompiler().compile(Parser.parse("white_elo >= 1000"));
    List<GameFeature> page1 = dao.query(allGames, 1, 0);
    List<GameFeature> page2 = dao.query(allGames, 1, 1);

    assertThat(page1).hasSize(1);
    assertThat(page2).hasSize(1);
    // game_url ASC tiebreaker: "aaa-first" < "zzz-last"
    assertThat(page1.get(0).gameUrl()).isEqualTo("https://chess.com/game/aaa-first");
    assertThat(page2.get(0).gameUrl()).isEqualTo("https://chess.com/game/zzz-last");
  }

  @Test
  public void deleteOccurrencesByGameUrls_thenReinsert_doesNotDuplicate() {
    // Regression test: re-indexing a partial month must not accumulate duplicate occurrences.
    // The fix is in IndexWorker.flushBatch: delete occurrences for each game_url before inserting.
    String gameUrl = "https://chess.com/game/reindex-dedup";
    dao.insertBatch(List.of(createGame(gameUrl)));

    GameFeatures.MotifOccurrence pin =
        new GameFeatures.MotifOccurrence(
            5, 3, "white", "Pin on c6", null, "Bb5", "nc6", false, false, "ABSOLUTE");
    Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrences =
        Map.of(gameUrl, Map.of(Motif.PIN, List.of(pin)));

    // First index run
    dao.insertOccurrencesBatch(occurrences);
    // Simulate re-index: delete then re-insert (what flushBatch now does)
    dao.deleteOccurrencesByGameUrls(List.of(gameUrl));
    dao.insertOccurrencesBatch(occurrences);

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    assertThat(result.get(gameUrl).get("pin")).hasSize(1);
  }

  @Test
  public void aggregate_countsGamesPerGroupOrderedByCountDesc() {
    dao.insertBatch(
        List.of(
            gameWithOpening("https://chess.com/game/agg-1", "Caro Kann Defense", "blitz"),
            gameWithOpening("https://chess.com/game/agg-2", "Caro Kann Defense", "blitz"),
            gameWithOpening("https://chess.com/game/agg-3", "Sicilian Defense", "blitz"),
            gameWithOpening("https://chess.com/game/agg-4", "Sicilian Defense", "rapid")));

    SqlCompiler compiler = new SqlCompiler();
    CompiledQuery compiled =
        compiler.compileAggregate(
            Parser.parse("time.class = \"blitz\""), List.of("opening.family"));
    List<AggregateRow> groups = dao.aggregate(compiled, List.of("opening_family"), 10);

    assertThat(groups).hasSize(2);
    assertThat(groups.get(0).group()).containsEntry("opening_family", "Caro Kann Defense");
    assertThat(groups.get(0).count()).isEqualTo(2);
    assertThat(groups.get(1).group()).containsEntry("opening_family", "Sicilian Defense");
    assertThat(groups.get(1).count()).isEqualTo(1);
  }

  @Test
  public void aggregate_respectsLimit() {
    dao.insertBatch(
        List.of(
            gameWithOpening("https://chess.com/game/lim-1", "Caro Kann Defense", "blitz"),
            gameWithOpening("https://chess.com/game/lim-2", "Sicilian Defense", "blitz"),
            gameWithOpening("https://chess.com/game/lim-3", "English Opening", "blitz")));

    SqlCompiler compiler = new SqlCompiler();
    CompiledQuery compiled =
        compiler.compileAggregate(Parser.parse("white_elo >= 1000"), List.of("opening_family"));
    List<AggregateRow> groups = dao.aggregate(compiled, List.of("opening_family"), 2);

    assertThat(groups).hasSize(2);
  }

  @Test
  public void aggregateTotals_reportsUntruncatedCounts() {
    dao.insertBatch(
        List.of(
            gameWithOpening("https://chess.com/game/tot-1", "Caro Kann Defense", "blitz"),
            gameWithOpening("https://chess.com/game/tot-2", "Caro Kann Defense", "blitz"),
            gameWithOpening("https://chess.com/game/tot-3", "Sicilian Defense", "blitz"),
            gameWithOpening("https://chess.com/game/tot-4", "English Opening", "blitz")));

    SqlCompiler compiler = new SqlCompiler();
    CompiledQuery compiled =
        compiler.compileAggregate(Parser.parse("white_elo >= 1000"), List.of("opening_family"));
    CompiledQuery totalsQuery =
        compiler.compileAggregateTotals(
            Parser.parse("white_elo >= 1000"), List.of("opening_family"));

    // A limit of 2 truncates one group; the totals still see all 3 groups / 4 games
    List<AggregateRow> groups = dao.aggregate(compiled, List.of("opening_family"), 2);
    GameFeatureStore.AggregateTotals totals = dao.aggregateTotals(totalsQuery);

    assertThat(groups).hasSize(2);
    assertThat(totals.totalGames()).isEqualTo(4);
    assertThat(totals.totalGroups()).isEqualTo(3);
  }

  /**
   * Grouping by opponent.title, through the real store, buckets each game by the *other* side's
   * title across both colors. No physical column can express this: grouping white_title or
   * black_title files the player's own title into the buckets on half the rows (here that would
   * read IM 4, which is hikaru, not his opponents). Untitled opponents land in a NULL group,
   * exactly as they do when grouping the physical nullable columns.
   */
  @Test
  public void aggregate_groupsByOpponentTitleAcrossBothColors() {
    dao.insertBatch(titledGames("opp"));

    SqlCompiler compiler = new SqlCompiler();
    CompiledQuery compiled =
        compiler.compileAggregate(
            Parser.parse("time.class = \"blitz\""), List.of("opponent.title"), "hikaru");
    List<AggregateRow> groups = dao.aggregate(compiled, List.of("opponent_title"), 10);

    // GM from both colors pools into one bucket; hikaru's IM appears in none of them.
    assertThat(groups).hasSize(3);
    assertThat(groups.get(0).group()).containsEntry("opponent_title", "GM");
    assertThat(groups.get(0).count()).isEqualTo(2);
    assertThat(groups)
        .anySatisfy(
            g -> {
              assertThat(g.group()).containsEntry("opponent_title", "FM");
              assertThat(g.count()).isEqualTo(1);
            })
        .anySatisfy(
            g -> {
              assertThat(g.group()).containsEntry("opponent_title", null);
              assertThat(g.count()).isEqualTo(1);
            })
        .noneSatisfy(g -> assertThat(g.group()).containsEntry("opponent_title", "IM"));

    CompiledQuery totalsQuery =
        compiler.compileAggregateTotals(
            Parser.parse("time.class = \"blitz\""), List.of("opponent.title"), "hikaru");
    GameFeatureStore.AggregateTotals totals = dao.aggregateTotals(totalsQuery);
    assertThat(totals.totalGroups()).isEqualTo(3);
    assertThat(totals.totalGames()).isEqualTo(4);
  }

  /**
   * The mirror of the opponent.title test above: me.title pools the player's own title from
   * whichever side they sat — four games, two colors, one IM bucket — and the opponents' titles
   * reach no bucket. Together the two tests pin that the two CASEs pick opposite sides of the same
   * rows.
   */
  @Test
  public void aggregate_groupsByMeTitleAcrossBothColors() {
    dao.insertBatch(titledGames("mt"));

    SqlCompiler compiler = new SqlCompiler();
    List<AggregateRow> groups =
        dao.aggregate(
            compiler.compileAggregate(
                Parser.parse("time.class = \"blitz\""), List.of("me.title"), "hikaru"),
            List.of("me_title"),
            10);

    assertThat(groups).hasSize(1);
    assertThat(groups.get(0).group()).containsEntry("me_title", "IM");
    assertThat(groups.get(0).count()).isEqualTo(4);
  }

  /**
   * Two perspective CASEs with different player-param counts in one grouping, executed for real:
   * opponent.title binds one player param, outcome binds two, and the participation guard prepends
   * two more — the bind-order contract SqlCompilerTest pins as strings, driven through the engine.
   * Tuple assertions are order-free on purpose: every group ties at count 1, and H2 and Postgres
   * disagree on where NULL sorts in an ASC tiebreak.
   */
  @Test
  public void aggregate_groupsByOpponentTitleAndOutcomeTogether() {
    dao.insertBatch(
        List.of(
            perspectiveGame(
                "https://chess.com/game/to-1", "hikaru", "gmfoe", "IM", "GM", "1-0", "Caro Kann"),
            perspectiveGame(
                "https://chess.com/game/to-2", "gmfoe2", "hikaru", "GM", "IM", "1-0", "Caro Kann"),
            perspectiveGame(
                "https://chess.com/game/to-3",
                "hikaru",
                "fmfoe",
                "IM",
                "FM",
                "1/2-1/2",
                "Caro Kann"),
            perspectiveGame(
                "https://chess.com/game/to-4",
                "untitled_foe",
                "hikaru",
                null,
                "IM",
                "0-1",
                "Caro Kann")));

    SqlCompiler compiler = new SqlCompiler();
    List<String> groupBy = List.of("opponent.title", "outcome");
    List<AggregateRow> groups =
        dao.aggregate(
            compiler.compileAggregate(Parser.parse("time.class = \"blitz\""), groupBy, "hikaru"),
            compiler.resolveGroupByColumns(groupBy),
            10);

    assertThat(groups).hasSize(4);
    assertThat(
            groups.stream()
                .map(
                    g ->
                        g.group().get("opponent_title")
                            + "/"
                            + g.group().get("outcome")
                            + "/"
                            + g.count()))
        .containsExactlyInAnyOrder("GM/win/1", "GM/loss/1", "FM/draw/1", "null/win/1");
  }

  /**
   * Opponent.elo, through the real store, buckets by the band's numeric lower bound across both
   * colors. 2400 and 2499 land in the same [2400, 2500) bucket while 2399 falls into [2300, 2400) —
   * the half-open boundary — and a NULL elo (chess.com omitted that side's rating data) pools into
   * a NULL bucket instead of vanishing. Keys come back as Integers, not strings: the numeric key is
   * what makes the ASC tiebreak sort bands numerically.
   */
  @Test
  public void aggregate_groupsByOpponentEloBucketsAcrossBothColors() {
    dao.insertBatch(bucketGames("eb"));

    SqlCompiler compiler = new SqlCompiler();
    ParsedQuery parsed = Parser.parse("time.class = \"blitz\"");
    List<String> groupBy = List.of("opponent.elo");
    List<AggregateRow> groups =
        dao.aggregate(
            compiler.compileAggregate(parsed, groupBy, "hikaru"),
            compiler.resolveGroupByColumns(groupBy),
            10);

    // 2450 (as White), 2499 (as Black), and boundary 2400 pool into one bucket; hikaru's own
    // 2800s reach no bucket. The count-1 groups tie, so those assertions are order-free (H2 and
    // Postgres disagree on where NULL sorts in an ASC tiebreak).
    assertThat(groups).hasSize(3);
    assertThat(groups.get(0).group()).containsEntry("opponent_elo", 2400);
    assertThat(groups.get(0).count()).isEqualTo(3);
    assertThat(groups)
        .anySatisfy(
            g -> {
              assertThat(g.group()).containsEntry("opponent_elo", 2300);
              assertThat(g.count()).isEqualTo(1);
            })
        .anySatisfy(
            g -> {
              assertThat(g.group()).containsEntry("opponent_elo", null);
              assertThat(g.count()).isEqualTo(1);
            });

    GameFeatureStore.AggregateTotals totals =
        dao.aggregateTotals(compiler.compileAggregateTotals(parsed, groupBy, "hikaru"));
    assertThat(totals.totalGroups()).isEqualTo(3);
    assertThat(totals.totalGames()).isEqualTo(5);

    // A caller-supplied width reshapes the bands: at 200 wide, 2400/2450/2499 stay together and
    // 2399 moves to [2200, 2400).
    List<String> wideBy = List.of("opponent.elo(200)");
    List<AggregateRow> wide =
        dao.aggregate(
            compiler.compileAggregate(parsed, wideBy, "hikaru"),
            compiler.resolveGroupByColumns(wideBy),
            10);
    assertThat(wide.get(0).group()).containsEntry("opponent_elo", 2400);
    assertThat(wide.get(0).count()).isEqualTo(3);
    assertThat(wide).anySatisfy(g -> assertThat(g.group()).containsEntry("opponent_elo", 2200));
  }

  /**
   * A bucket term and a categorical perspective term in one grouping, executed for real: the
   * arithmetic-wrapped alias and the plain CASE alias must both survive GROUP BY and the ORDER BY
   * tiebreak in the same statement, with the bucket's player param binding before me.color's.
   * Order-free tuples because every count-1 group ties (H2 and Postgres disagree on NULL order).
   */
  @Test
  public void aggregate_groupsByMeColorAndOpponentEloBucketsTogether() {
    dao.insertBatch(bucketGames("cb"));

    SqlCompiler compiler = new SqlCompiler();
    List<String> groupBy = List.of("me.color", "opponent.elo(200)");
    List<AggregateRow> groups =
        dao.aggregate(
            compiler.compileAggregate(Parser.parse("time.class = \"blitz\""), groupBy, "hikaru"),
            compiler.resolveGroupByColumns(groupBy),
            10);

    assertThat(
            groups.stream()
                .map(
                    g ->
                        g.group().get("me_color")
                            + "/"
                            + g.group().get("opponent_elo")
                            + "/"
                            + g.count()))
        .containsExactlyInAnyOrder("white/2400/2", "black/2400/1", "white/2200/1", "black/null/1");
  }

  /**
   * The bucket arithmetic at the INT extremes, executed rather than reasoned about: {@code (elo /
   * width) * width} must stay in INT range for every elo the column can hold (truncating division
   * bounds the product by the input, so Integer.MAX_VALUE at width 100 keys 2147483600, and at
   * width Integer.MAX_VALUE keys itself), and a negative elo — impossible via ingest but not
   * constrained by the schema — truncates toward zero (-150 keys -100, not FLOOR's -200).
   * PostgresAggregateCompatTest runs the same fixture on the real dialect; if either engine widened
   * the arithmetic or raised on the multiply, one of these buckets would come back a different
   * type, a different key, or not at all.
   */
  @Test
  public void aggregate_bucketArithmeticAtIntegerExtremes() {
    dao.insertBatch(
        List.of(
            eloGame("https://chess.com/game/ex-1", "hikaru", "a", 2800, 2450),
            eloGame("https://chess.com/game/ex-2", "hikaru", "b", 2800, Integer.MAX_VALUE),
            eloGame("https://chess.com/game/ex-3", "hikaru", "c", 2800, -150)));

    SqlCompiler compiler = new SqlCompiler();
    ParsedQuery parsed = Parser.parse("time.class = \"blitz\"");
    List<String> groupBy = List.of("opponent.elo");
    List<AggregateRow> groups =
        dao.aggregate(
            compiler.compileAggregate(parsed, groupBy, "hikaru"),
            compiler.resolveGroupByColumns(groupBy),
            10);

    assertThat(groups).hasSize(3);
    assertThat(groups)
        .anySatisfy(g -> assertThat(g.group()).containsEntry("opponent_elo", 2400))
        .anySatisfy(g -> assertThat(g.group()).containsEntry("opponent_elo", 2147483600))
        .anySatisfy(g -> assertThat(g.group()).containsEntry("opponent_elo", -100));

    // A width at Integer.MAX_VALUE collapses every smaller elo to bucket 0 and keys the
    // MAX_VALUE elo as itself (MAX / MAX * MAX) — no overflow, no widening, still two groups.
    List<String> maxWidth = List.of("opponent.elo(" + Integer.MAX_VALUE + ")");
    List<AggregateRow> collapsed =
        dao.aggregate(
            compiler.compileAggregate(parsed, maxWidth, "hikaru"),
            compiler.resolveGroupByColumns(maxWidth),
            10);
    assertThat(collapsed).hasSize(2);
    assertThat(collapsed.get(0).group()).containsEntry("opponent_elo", 0);
    assertThat(collapsed.get(0).count()).isEqualTo(2);
    assertThat(collapsed.get(1).group()).containsEntry("opponent_elo", Integer.MAX_VALUE);
    assertThat(collapsed.get(1).count()).isEqualTo(1);
  }

  /**
   * The me.elo mirror of the bucket test above: the CASE picks the player's own side, so five games
   * at 2800 — three as White, two as Black, one against a NULL-elo opponent — are one [2800, 2900)
   * bucket. If the CASE picked the wrong side the groups would fragment into the opponents'
   * buckets.
   */
  @Test
  public void aggregate_groupsByMeEloBucketsAcrossBothColors() {
    dao.insertBatch(bucketGames("mb"));

    SqlCompiler compiler = new SqlCompiler();
    List<AggregateRow> groups =
        dao.aggregate(
            compiler.compileAggregate(
                Parser.parse("time.class = \"blitz\""), List.of("me.elo"), "hikaru"),
            compiler.resolveGroupByColumns(List.of("me.elo")),
            10);

    assertThat(groups).hasSize(1);
    assertThat(groups.get(0).group()).containsEntry("me_elo", 2800);
    assertThat(groups.get(0).count()).isEqualTo(5);
  }

  /**
   * One half of the dialect divergence the order-free sibling assertions accommodate, pinned
   * deliberately: H2 sorts a NULL group key FIRST in the ASC tiebreak, while the Postgres twin
   * (PostgresAggregateCompatTest.nullGroupKeySortsLastInTheTiebreakOnPostgres) pins LAST. The
   * compiler emits no NULLS FIRST/LAST normalization on purpose; if it ever does, or if either
   * engine changes its default, exactly one of the twins fails and the recorded divergence gets
   * re-examined.
   */
  @Test
  public void aggregate_nullGroupKeySortsFirstInTheTiebreakOnH2() {
    dao.insertBatch(
        List.of(
            perspectiveGame(
                "https://chess.com/game/nf-1", "hikaru", "fmfoe", "IM", "FM", "1-0", "Caro Kann"),
            perspectiveGame(
                "https://chess.com/game/nf-2",
                "untitled_foe",
                "hikaru",
                null,
                "IM",
                "0-1",
                "Caro Kann")));

    SqlCompiler compiler = new SqlCompiler();
    List<AggregateRow> groups =
        dao.aggregate(
            compiler.compileAggregate(
                Parser.parse("time.class = \"blitz\""), List.of("opponent.title"), "hikaru"),
            List.of("opponent_title"),
            10);

    // Both groups tie at count 1, so the order IS the ASC tiebreak — and on H2 the NULL key
    // leads.
    assertThat(groups.stream().map(g -> g.group().get("opponent_title")))
        .containsExactly(null, "FM");
  }

  /**
   * opponent.username groups by the stored casing, exactly as documented: the perspective filter
   * matches the player case-insensitively (hikaru/Hikaru is one player here), but group keys are
   * the raw stored values, so one opponent stored under two casings forms two groups — the same
   * trap opening_family's unnormalized values set.
   */
  @Test
  public void aggregate_opponentUsernameGroupsKeepStoredCasingDistinct() {
    dao.insertBatch(
        List.of(
            eloGame("https://chess.com/game/case-1", "hikaru", "Foe", 2800, 2400),
            eloGame("https://chess.com/game/case-2", "foe", "Hikaru", 2400, 2800)));

    SqlCompiler compiler = new SqlCompiler();
    List<AggregateRow> groups =
        dao.aggregate(
            compiler.compileAggregate(
                Parser.parse("time.class = \"blitz\""), List.of("opponent.username"), "hikaru"),
            compiler.resolveGroupByColumns(List.of("opponent.username")),
            10);

    assertThat(groups.stream().map(g -> g.group().get("opponent_username")))
        .containsExactlyInAnyOrder("Foe", "foe");
    assertThat(groups).allSatisfy(g -> assertThat(g.count()).isEqualTo(1));
  }

  @Test
  public void aggregateTotals_zeroWhenNoGamesMatch() {
    SqlCompiler compiler = new SqlCompiler();
    GameFeatureStore.AggregateTotals totals =
        dao.aggregateTotals(
            compiler.compileAggregateTotals(
                Parser.parse("white.username = \"nobody\""), List.of("opening_family")));

    assertThat(totals.totalGames()).isZero();
    assertThat(totals.totalGroups()).isZero();
  }

  @Test
  public void dateAndMonthScoping_filterByPlayedAtOnH2() {
    Instant june = Instant.parse("2026-06-15T12:00:00Z");
    // Exactly midnight on the month boundary — belongs to July, not June
    Instant julyBoundary = Instant.parse("2026-07-01T00:00:00Z");
    dao.insertBatch(
        List.of(
            createGameAt("https://chess.com/game/june", june),
            createGameAt("https://chess.com/game/july", julyBoundary)));

    SqlCompiler compiler = new SqlCompiler();

    List<GameFeature> juneGames =
        dao.query(compiler.compile(Parser.parse("month = \"2026-06\"")), 10, 0);
    assertThat(juneGames.stream().map(GameFeature::gameUrl))
        .containsExactly("https://chess.com/game/june");

    List<GameFeature> julyGames =
        dao.query(compiler.compile(Parser.parse("month = \"2026-07\"")), 10, 0);
    assertThat(julyGames.stream().map(GameFeature::gameUrl))
        .containsExactly("https://chess.com/game/july");

    List<GameFeature> fromJuly =
        dao.query(compiler.compile(Parser.parse("date >= \"2026-07-01\"")), 10, 0);
    assertThat(fromJuly.stream().map(GameFeature::gameUrl))
        .containsExactly("https://chess.com/game/july");

    // Inclusive upper bound covers the entire day
    List<GameFeature> throughJune15 =
        dao.query(compiler.compile(Parser.parse("date <= \"2026-06-15\"")), 10, 0);
    assertThat(throughJune15.stream().map(GameFeature::gameUrl))
        .containsExactly("https://chess.com/game/june");

    List<GameFeature> onJune15 =
        dao.query(compiler.compile(Parser.parse("date = \"2026-06-15\"")), 10, 0);
    assertThat(onJune15.stream().map(GameFeature::gameUrl))
        .containsExactly("https://chess.com/game/june");

    List<GameFeature> notJune15 =
        dao.query(compiler.compile(Parser.parse("date != \"2026-06-15\"")), 10, 0);
    assertThat(notJune15.stream().map(GameFeature::gameUrl))
        .containsExactly("https://chess.com/game/july");

    List<GameFeature> juneRange =
        dao.query(
            compiler.compile(Parser.parse("date >= \"2026-06-01\" AND date < \"2026-07-01\"")),
            10,
            0);
    assertThat(juneRange.stream().map(GameFeature::gameUrl))
        .containsExactly("https://chess.com/game/june");
  }

  /**
   * Every {@code date} operator against rows sitting exactly on the day's half-open boundaries. An
   * off-by-a-day in the rewrite (e.g. {@code <=} binding start-of-day instead of start-of-next-day)
   * puts one of these rows on the wrong side, which the equality assertions below catch.
   */
  @Test
  public void dateOperators_boundaryInstantsOnH2() {
    dao.insertBatch(
        List.of(
            createGameAt("prevEnd", Instant.parse("2026-06-14T23:59:59.999Z")),
            createGameAt("dayStart", Instant.parse("2026-06-15T00:00:00.000Z")),
            createGameAt("dayMid", Instant.parse("2026-06-15T12:00:00.000Z")),
            createGameAt("dayEnd", Instant.parse("2026-06-15T23:59:59.999Z")),
            createGameAt("nextStart", Instant.parse("2026-06-16T00:00:00.000Z")),
            // played_at is a nullable column and insertBatch binds null for it
            createGameAt("noPlayedAt", null)));

    assertThat(urlsMatching("date = \"2026-06-15\""))
        .containsExactly("dayEnd", "dayMid", "dayStart");
    assertThat(urlsMatching("date != \"2026-06-15\"")).containsExactly("nextStart", "prevEnd");
    assertThat(urlsMatching("date < \"2026-06-15\"")).containsExactly("prevEnd");
    assertThat(urlsMatching("date <= \"2026-06-15\""))
        .containsExactly("dayEnd", "dayMid", "dayStart", "prevEnd");
    assertThat(urlsMatching("date > \"2026-06-15\"")).containsExactly("nextStart");
    assertThat(urlsMatching("date >= \"2026-06-15\""))
        .containsExactly("dayEnd", "dayMid", "dayStart", "nextStart");
    assertThat(urlsMatching("month = \"2026-06\""))
        .containsExactly("dayEnd", "dayMid", "dayStart", "nextStart", "prevEnd");

    // The row is really there — it is only invisible to date predicates. SQL three-valued logic
    // means NULL played_at satisfies neither `date = D` nor `date != D`, so a game with no
    // timestamp silently drops out of any date-scoped query, including the negated one.
    SqlCompiler compiler = new SqlCompiler();
    assertThat(
            dao.aggregateTotals(
                    compiler.compileAggregateTotals(
                        Parser.parse("white_elo >= 1000"), List.of("platform")))
                .totalGames())
        .isEqualTo(6);
  }

  /**
   * totalGames is SUM(group_count) over the inner grouped query, so it must equal the number of
   * rows matching the filter — including rows whose group column is NULL (SQL groups those into one
   * NULL group rather than dropping them) and excluding rows the filter rejects.
   */
  @Test
  public void aggregateTotals_countNullGroupValuesAndExcludeFilteredRows() {
    dao.insertBatch(
        List.of(
            gameWithOpening("https://chess.com/game/nt-1", "Caro Kann Defense", "blitz"),
            gameWithOpening("https://chess.com/game/nt-2", "Caro Kann Defense", "blitz"),
            gameWithOpening("https://chess.com/game/nt-3", "Sicilian Defense", "blitz"),
            // opening_family is NULL on these two: they form one additional group, not zero
            createGame("https://chess.com/game/nt-4"),
            createGame("https://chess.com/game/nt-5"),
            // filtered out by time_class, so it must not reach either total
            gameWithOpening("https://chess.com/game/nt-6", "English Opening", "bullet")));

    SqlCompiler compiler = new SqlCompiler();
    ParsedQuery parsed = Parser.parse("time.class = \"blitz\"");
    List<AggregateRow> groups =
        dao.aggregate(
            compiler.compileAggregate(parsed, List.of("opening_family")),
            List.of("opening_family"),
            10);
    GameFeatureStore.AggregateTotals totals =
        dao.aggregateTotals(compiler.compileAggregateTotals(parsed, List.of("opening_family")));

    assertThat(groups.stream().map(g -> g.group().get("opening_family")))
        .containsExactlyInAnyOrder("Caro Kann Defense", "Sicilian Defense", null);
    assertThat(groups.stream().mapToLong(AggregateRow::count).sum()).isEqualTo(5);
    assertThat(totals.totalGames()).isEqualTo(5);
    assertThat(totals.totalGroups()).isEqualTo(3);
  }

  /**
   * compileAggregate puts perspective group expressions in the SELECT list and has GROUP BY / ORDER
   * BY reference the alias. H2 and Postgres resolve such a name differently when a physical column
   * shares it: H2 binds it to the SELECT alias, Postgres binds it to the input column and then
   * fails with "must appear in the GROUP BY clause". So the aliases must not collide with any
   * game_features column — this pins that, and fires if a column named outcome or me_color is ever
   * added to the schema.
   */
  @Test
  public void perspectiveGroupByAliases_doNotCollideWithPhysicalColumns() throws Exception {
    List<String> aliases = new SqlCompiler().resolveGroupByColumns(List.of("me.color", "outcome"));
    assertThat(aliases).containsExactly("me_color", "outcome");

    List<String> columns = new ArrayList<>();
    try (var conn = testDb.dataSource().getConnection();
        var rs = conn.getMetaData().getColumns(null, null, "GAME_FEATURES", null)) {
      while (rs.next()) {
        columns.add(rs.getString("COLUMN_NAME").toLowerCase());
      }
    }
    assertThat(columns).isNotEmpty().doesNotContainAnyElementsOf(aliases);
  }

  /** Game URLs matching a ChessQL filter, sorted so assertions read as a set. */
  private List<String> urlsMatching(String chessql) {
    return dao.query(new SqlCompiler().compile(Parser.parse(chessql)), 50, 0).stream()
        .map(GameFeature::gameUrl)
        .sorted()
        .toList();
  }

  @Test
  public void aggregate_groupByMeColorAndOutcome_splitsWinLossByColorOnH2() {
    dao.insertBatch(
        List.of(
            // hikaru as white: two wins
            perspectiveGame(
                "https://chess.com/game/gc1", "hikaru", "a", null, null, "1-0", "Caro Kann"),
            perspectiveGame(
                "https://chess.com/game/gc2", "hikaru", "b", null, null, "1-0", "Sicilian"),
            // hikaru as black: one win, one loss
            perspectiveGame(
                "https://chess.com/game/gc3", "c", "hikaru", null, null, "0-1", "English"),
            perspectiveGame(
                "https://chess.com/game/gc4", "d", "hikaru", null, null, "1-0", "English"),
            // not hikaru's game — excluded by the participation guard
            perspectiveGame("https://chess.com/game/gc5", "x", "y", null, null, "1-0", "English")));

    SqlCompiler compiler = new SqlCompiler();
    CompiledQuery compiled =
        compiler.compileAggregate(
            Parser.parse("time.class = \"blitz\""), List.of("me.color", "outcome"), "hikaru");
    List<AggregateRow> groups = dao.aggregate(compiled, List.of("me_color", "outcome"), 10);

    // (white, win, 2) leads on count; the single-count groups tiebreak on me_color/outcome ASC
    assertThat(groups).hasSize(3);
    assertThat(groups.get(0).group())
        .containsEntry("me_color", "white")
        .containsEntry("outcome", "win");
    assertThat(groups.get(0).count()).isEqualTo(2);
    assertThat(groups.get(1).group())
        .containsEntry("me_color", "black")
        .containsEntry("outcome", "loss");
    assertThat(groups.get(1).count()).isEqualTo(1);
    assertThat(groups.get(2).group())
        .containsEntry("me_color", "black")
        .containsEntry("outcome", "win");
    assertThat(groups.get(2).count()).isEqualTo(1);

    GameFeatureStore.AggregateTotals totals =
        dao.aggregateTotals(
            compiler.compileAggregateTotals(
                Parser.parse("time.class = \"blitz\""), List.of("me.color", "outcome"), "hikaru"));
    assertThat(totals.totalGames()).isEqualTo(4);
    assertThat(totals.totalGroups()).isEqualTo(3);
  }

  @Test
  public void perspectiveFields_resolveAgainstPlayerOnH2() {
    dao.insertBatch(
        List.of(
            // hikaru wins as white; untitled opponent
            perspectiveGame(
                "https://chess.com/game/p1",
                "hikaru",
                "opp1",
                null,
                null,
                "1-0",
                "Caro Kann Defense"),
            // hikaru wins as black against a GM
            perspectiveGame(
                "https://chess.com/game/p2",
                "opp2",
                "hikaru",
                "GM",
                null,
                "0-1",
                "Sicilian Defense"),
            // hikaru loses as black
            perspectiveGame(
                "https://chess.com/game/p3",
                "opp3",
                "hikaru",
                null,
                null,
                "1-0",
                "Caro Kann Defense"),
            // draw as white
            perspectiveGame(
                "https://chess.com/game/p4",
                "hikaru",
                "opp4",
                null,
                null,
                "1/2-1/2",
                "English Opening"),
            // a game hikaru did not play — must be excluded by the participation guard
            perspectiveGame(
                "https://chess.com/game/p5",
                "someone",
                "else",
                null,
                null,
                "1-0",
                "Caro Kann Defense")));

    SqlCompiler compiler = new SqlCompiler();

    List<GameFeature> wins =
        dao.query(compiler.compile(Parser.parse("outcome = \"win\""), "Hikaru"), 10, 0);
    assertThat(wins.stream().map(GameFeature::gameUrl))
        .containsExactlyInAnyOrder("https://chess.com/game/p1", "https://chess.com/game/p2");

    List<GameFeature> losses =
        dao.query(compiler.compile(Parser.parse("outcome = \"loss\""), "hikaru"), 10, 0);
    assertThat(losses.stream().map(GameFeature::gameUrl))
        .containsExactly("https://chess.com/game/p3");

    List<GameFeature> draws =
        dao.query(compiler.compile(Parser.parse("outcome = \"draw\""), "hikaru"), 10, 0);
    assertThat(draws.stream().map(GameFeature::gameUrl))
        .containsExactly("https://chess.com/game/p4");

    List<GameFeature> asWhite =
        dao.query(compiler.compile(Parser.parse("me.color = \"white\""), "hikaru"), 10, 0);
    assertThat(asWhite.stream().map(GameFeature::gameUrl))
        .containsExactlyInAnyOrder("https://chess.com/game/p1", "https://chess.com/game/p4");

    List<GameFeature> vsGm =
        dao.query(compiler.compile(Parser.parse("opponent.title = \"GM\""), "hikaru"), 10, 0);
    assertThat(vsGm.stream().map(GameFeature::gameUrl))
        .containsExactly("https://chess.com/game/p2");

    List<AggregateRow> winsByFamily =
        dao.aggregate(
            compiler.compileAggregate(
                Parser.parse("outcome = \"win\""), List.of("opening_family"), "hikaru"),
            List.of("opening_family"),
            10);
    assertThat(winsByFamily).hasSize(2);
    for (AggregateRow row : winsByFamily) {
      assertThat(row.group().get("opening_family")).isIn("Caro Kann Defense", "Sicilian Defense");
      assertThat(row.count()).isEqualTo(1);
    }
  }

  @Test
  public void perspectiveOutcomeUnknownAndOrGuard_onH2() {
    dao.insertBatch(
        List.of(
            // aborted game — result "*" must classify as unknown, not loss
            perspectiveGame(
                "https://chess.com/game/u1",
                "hikaru",
                "opp1",
                null,
                null,
                "*",
                "Caro Kann Defense"),
            // hikaru wins as white
            perspectiveGame(
                "https://chess.com/game/u2",
                "hikaru",
                "opp2",
                null,
                null,
                "1-0",
                "Sicilian Defense"),
            // a 2900-elo game hikaru did not play: matches the white.elo OR-branch below but must
            // be excluded by the participation guard
            new GameFeature(
                null,
                requestId,
                "https://chess.com/game/u3",
                "CHESS_COM",
                "someone",
                "else",
                2900,
                2850,
                null,
                null,
                "blitz",
                "B10",
                "English Opening Some Line",
                "English Opening",
                "1-0",
                Instant.now(),
                20,
                Instant.now(),
                "pgn")));

    SqlCompiler compiler = new SqlCompiler();

    List<GameFeature> unknown =
        dao.query(compiler.compile(Parser.parse("outcome = \"unknown\""), "hikaru"), 10, 0);
    assertThat(unknown.stream().map(GameFeature::gameUrl))
        .containsExactly("https://chess.com/game/u1");

    for (String outcome : List.of("win", "loss", "draw")) {
      List<GameFeature> rows =
          dao.query(
              compiler.compile(Parser.parse("outcome = \"" + outcome + "\""), "hikaru"), 10, 0);
      assertThat(rows.stream().map(GameFeature::gameUrl))
          .as("outcome = %s must not include the aborted game", outcome)
          .doesNotContain("https://chess.com/game/u1");
    }

    // OR with a non-perspective branch: u3 matches white.elo > 2800 but hikaru didn't play it
    List<GameFeature> winsOrHighElo =
        dao.query(
            compiler.compile(Parser.parse("outcome = \"win\" OR white.elo > 2800"), "hikaru"),
            10,
            0);
    assertThat(winsOrHighElo.stream().map(GameFeature::gameUrl))
        .containsExactly("https://chess.com/game/u2");
  }

  private GameFeature perspectiveGame(
      String url,
      String white,
      String black,
      String whiteTitle,
      String blackTitle,
      String result,
      String openingFamily) {
    return perspectiveGame(
        url, white, black, 1500, 1500, whiteTitle, blackTitle, result, openingFamily);
  }

  private GameFeature eloGame(
      String url, String white, String black, Integer whiteElo, Integer blackElo) {
    return perspectiveGame(url, white, black, whiteElo, blackElo, null, null, "1-0", "Caro Kann");
  }

  private GameFeature perspectiveGame(
      String url,
      String white,
      String black,
      Integer whiteElo,
      Integer blackElo,
      String whiteTitle,
      String blackTitle,
      String result,
      String openingFamily) {
    return new GameFeature(
        null,
        requestId,
        url,
        "CHESS_COM",
        white,
        black,
        whiteElo,
        blackElo,
        whiteTitle,
        blackTitle,
        "blitz",
        "B10",
        openingFamily + " Some Line",
        openingFamily,
        result,
        Instant.now(),
        20,
        Instant.now(),
        "pgn");
  }

  /**
   * The four-game titled fixture the title-grouping mirrors share: hikaru (IM) faces a GM as White
   * and as Black — the mixed "Hikaru" casing doubling as the case-insensitive player-match pin —
   * plus an FM and an untitled opponent.
   */
  private List<GameFeature> titledGames(String prefix) {
    String base = "https://chess.com/game/" + prefix;
    return List.of(
        perspectiveGame(base + "-1", "hikaru", "gmfoe", "IM", "GM", "1-0", "Caro Kann"),
        perspectiveGame(base + "-2", "gmfoe2", "Hikaru", "GM", "IM", "0-1", "Caro Kann"),
        perspectiveGame(base + "-3", "hikaru", "fmfoe", "IM", "FM", "1-0", "Caro Kann"),
        perspectiveGame(base + "-4", "untitled_foe", "hikaru", null, "IM", "0-1", "Caro Kann"));
  }

  /**
   * The five-game elo fixture the bucket tests share: hikaru at 2800 from both colors (mixed
   * "Hikaru" casing, the case-insensitive pin), opponents at 2450/2499 (one per color, pooling into
   * [2400, 2500)), boundary values 2400 and 2399, and one NULL-elo opponent.
   */
  private List<GameFeature> bucketGames(String prefix) {
    String base = "https://chess.com/game/" + prefix;
    return List.of(
        eloGame(base + "-1", "hikaru", "a", 2800, 2450),
        eloGame(base + "-2", "b", "Hikaru", 2499, 2800),
        eloGame(base + "-3", "hikaru", "c", 2800, 2400),
        eloGame(base + "-4", "hikaru", "d", 2800, 2399),
        eloGame(base + "-5", "e", "hikaru", null, 2800));
  }

  private GameFeature gameWithOpening(String url, String openingFamily, String timeClass) {
    return new GameFeature(
        null,
        requestId,
        url,
        "CHESS_COM",
        "w",
        "b",
        1500,
        1500,
        null,
        null,
        timeClass,
        "B10",
        openingFamily + " Some Line",
        openingFamily,
        "1-0",
        Instant.now(),
        20,
        Instant.now(),
        "pgn");
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
        null,
        null,
        "blitz",
        "B00",
        null,
        null,
        "1-0",
        Instant.now(),
        20,
        Instant.now(),
        "pgn");
  }

  private GameFeature createGameAt(String url, Instant playedAt) {
    return new GameFeature(
        null,
        requestId,
        url,
        "chess.com",
        "white",
        "black",
        1500,
        1480,
        null,
        null,
        "blitz",
        "A00",
        null,
        null,
        "1-0",
        playedAt,
        30,
        Instant.now(),
        "1. e4 e5 *");
  }

  @Test
  public void insertBatch_roundTripsTitleAndOpeningColumns() {
    GameFeature game =
        new GameFeature(
            null,
            requestId,
            "https://chess.com/game/titled",
            "CHESS_COM",
            "hikaru",
            "rpragchess",
            2800,
            2750,
            "GM",
            "GM",
            "blitz",
            "B10",
            "Caro Kann Defense Two Knights Attack",
            "Caro Kann Defense",
            "1-0",
            Instant.now(),
            40,
            Instant.now(),
            "pgn");
    dao.insertBatch(List.of(game));

    CompiledQuery byTitleAndFamily =
        new SqlCompiler()
            .compile(
                Parser.parse("black.title = \"GM\" AND opening.family = \"caro kann defense\""));
    List<GameFeature> rows = dao.query(byTitleAndFamily, 10, 0);

    assertThat(rows).hasSize(1);
    GameFeature row = rows.get(0);
    assertThat(row.whiteTitle()).isEqualTo("GM");
    assertThat(row.blackTitle()).isEqualTo("GM");
    assertThat(row.openingName()).isEqualTo("Caro Kann Defense Two Knights Attack");
    assertThat(row.openingFamily()).isEqualTo("Caro Kann Defense");
  }
}
