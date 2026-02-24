package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
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
        new GameFeatures.MotifOccurrence(5, 3, "white", "Knight pinned on c6");
    GameFeatures.MotifOccurrence occ2 =
        new GameFeatures.MotifOccurrence(12, 6, "black", "Discovered check");
    Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences =
        Map.of(
            Motif.PIN, List.of(occ1),
            Motif.DISCOVERED_CHECK, List.of(occ2));

    dao.insertOccurrences(gameUrl, occurrences);

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));

    assertThat(result).containsKey(gameUrl);
    Map<String, List<OccurrenceRow>> byMotif = result.get(gameUrl);
    assertThat(byMotif).containsKey("pin");
    assertThat(byMotif.get("pin")).containsExactly(new OccurrenceRow(3, "Knight pinned on c6"));
    assertThat(byMotif).containsKey("discovered_check");
    assertThat(byMotif.get("discovered_check"))
        .containsExactly(new OccurrenceRow(6, "Discovered check"));
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
        new GameFeatures.MotifOccurrence(0, 0, "white", "initial");
    Map<Motif, List<GameFeatures.MotifOccurrence>> onlyPlyZero =
        Map.of(Motif.CHECK, List.of(atPlyZero));

    dao.insertOccurrences(gameUrl, onlyPlyZero);

    Map<String, Map<String, List<OccurrenceRow>>> result = dao.queryOccurrences(List.of(gameUrl));
    // No rows inserted (ply 0 skipped), so no occurrences for this game
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
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        Instant.now(),
        "pgn");
  }
}
