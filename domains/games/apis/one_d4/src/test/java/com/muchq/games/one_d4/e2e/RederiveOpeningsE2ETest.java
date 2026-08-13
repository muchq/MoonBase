package com.muchq.games.one_d4.e2e;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.AdminController;
import com.muchq.games.one_d4.api.dto.AggregateRow;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.RederiveResponse;
import com.muchq.games.one_d4.db.GameFeatureDao;
import com.muchq.games.one_d4.db.TestDb;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.GameReplayer;
import com.muchq.games.one_d4.engine.PgnParser;
import java.time.Instant;
import java.util.List;
import java.util.UUID;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * The symptom #1350 is about, through the real store: a derivation change leaves the table split
 * across two group keys for one opening, and until every row is corrected an exact-match filter
 * misses half of them.
 *
 * <p>This is what the backfill window costs. #1344 verified it on the frozen corpus — three
 * families each carrying an extra key (`Owens Defense` alongside `Owens Defense...5.Bd3`, and so
 * on) — and closing that window used to mean refetching every month from chess.com. The endpoint
 * under test does it with one pass over the table, which is the whole point: the corrected value is
 * a function of data already stored.
 *
 * <p>The controller tests pin the loop's arithmetic against a fake, and the DAO tests pin the SQL.
 * Neither says the two together fix what a caller sees, which is a grouped query returning one key
 * where it returned two.
 */
public class RederiveOpeningsE2ETest {

  private GameFeatureDao store;
  private AdminController admin;
  private UUID requestId;

  @BeforeEach
  public void setUp() {
    TestDb testDb = TestDb.create("rederive_openings_e2e");
    store = new GameFeatureDao(testDb.jdbi(), true);
    admin =
        new AdminController(
            store, new FeatureExtractor(new PgnParser(), new GameReplayer(), List.of()));

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
  public void rederiveCollapsesAFamilySplitAcrossTwoGroupKeys() {
    // Two games in the same opening, indexed either side of a derivation change: one row kept the
    // move continuation in its family, the other did not.
    store.insertBatch(
        List.of(
            game(
                "https://chess.com/game/r-1", "Owens Defense 3.Nc3 e6", "Owens Defense...3.Nc3-e6"),
            game("https://chess.com/game/r-2", "Owens Defense 5.Bd3", "Owens Defense")));

    assertThat(familyCounts()).hasSize(2);
    assertThat(gamesMatchingFamily("Owens Defense"))
        .as("an exact-match filter sees only the corrected row")
        .isEqualTo(1);

    RederiveResponse response = admin.rederiveOpenings();

    assertThat(response.gamesScanned()).isEqualTo(2);
    assertThat(response.gamesUpdated()).isEqualTo(1);
    assertThat(familyCounts()).hasSize(1);
    assertThat(gamesMatchingFamily("Owens Defense"))
        .as("both rows now answer the same filter")
        .isEqualTo(2);
  }

  /** Nothing to correct is the ordinary case, and it must cost one scan and no writes. */
  @Test
  public void rederiveOnAnAlreadyCorrectTableChangesNothing() {
    store.insertBatch(
        List.of(
            game(
                "https://chess.com/game/r-3",
                "Caro Kann Defense Two Knights",
                "Caro Kann Defense")));

    RederiveResponse response = admin.rederiveOpenings();

    assertThat(response.gamesScanned()).isEqualTo(1);
    assertThat(response.gamesUpdated()).isZero();
    assertThat(gamesMatchingFamily("Caro Kann Defense")).isEqualTo(1);
  }

  private List<AggregateRow> familyCounts() {
    SqlCompiler compiler = new SqlCompiler();
    return store.aggregate(
        compiler.compileAggregate(Parser.parse("num.moves >= 0"), List.of("opening_family")),
        List.of("opening_family"),
        50);
  }

  private long gamesMatchingFamily(String family) {
    return store
        .query(
            new SqlCompiler().compile(Parser.parse("opening.family = \"" + family + "\"")), 50, 0)
        .size();
  }

  private GameFeature game(String url, String openingName, String openingFamily) {
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
        openingName,
        openingFamily,
        "1-0",
        Instant.parse("2024-01-15T12:00:00Z"),
        20,
        Instant.now(),
        "pgn");
  }
}
