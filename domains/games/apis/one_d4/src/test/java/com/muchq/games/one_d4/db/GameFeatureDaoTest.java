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
