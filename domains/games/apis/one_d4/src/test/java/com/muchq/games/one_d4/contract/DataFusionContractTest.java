package com.muchq.games.one_d4.contract;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.chessql.compiler.DataFusionSqlCompiler;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.db.DataSourceFactory;
import com.muchq.games.one_d4.db.GameFeatureDao;
import com.muchq.games.one_d4.db.Migration;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.sql.DataSource;
import org.junit.Before;
import org.junit.Test;

/**
 * Contract test: verifies that {@link DataFusionSqlCompiler} produces SQL that returns the same
 * result set as {@link SqlCompiler} when executed against H2. Covers all 16 motifs, comparison, IN,
 * AND, OR, NOT, sequence, and ORDER BY expressions.
 *
 * <p>Both compilers emit standard SQL with identical semantics. SqlCompiler uses JDBC {@code ?}
 * bind parameters; DataFusionSqlCompiler inlines literal values. H2 accepts both forms.
 */
public class DataFusionContractTest {

  private GameFeatureDao dao;
  private final SqlCompiler sqlCompiler = new SqlCompiler();
  private final DataFusionSqlCompiler dfCompiler = new DataFusionSqlCompiler();
  private UUID requestId;

  @Before
  public void setUp() {
    String jdbcUrl = "jdbc:h2:mem:df_contract_" + System.currentTimeMillis() + ";DB_CLOSE_DELAY=-1";
    DataSource dataSource = DataSourceFactory.create(jdbcUrl, "sa", "");
    new Migration(dataSource, true).run();

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

    dao = new GameFeatureDao(dataSource, true);
    seedData();
  }

  // ===  Seeding ===

  private void seedData() {
    // Stored motifs — one game per motif
    seed(
        "game-pin",
        "CHESS_COM",
        "hikaru",
        "magnus",
        2800,
        2850,
        "B90",
        "1-0",
        Map.of(Motif.PIN, List.of(occ(5, 3))));
    // game-check has 2 occurrences; game-pin-check has 1 — used for ORDER BY ordering tests
    seed(
        "game-check",
        "CHESS_COM",
        "hikaru",
        "user2",
        2800,
        1500,
        "C00",
        "1-0",
        Map.of(Motif.CHECK, List.of(occ(7, 4), occ(11, 6))));
    seed(
        "game-cross-pin",
        "CHESS_COM",
        "user3",
        "user4",
        1600,
        1600,
        "D00",
        "0-1",
        Map.of(Motif.CROSS_PIN, List.of(occ(3, 2))));
    seed(
        "game-skewer",
        "LICHESS",
        "user5",
        "user6",
        1700,
        1700,
        "E00",
        "1-0",
        Map.of(Motif.SKEWER, List.of(occ(9, 5))));
    seed(
        "game-promotion",
        "CHESS_COM",
        "user7",
        "user8",
        1400,
        1400,
        "B00",
        "1-0",
        Map.of(Motif.PROMOTION, List.of(occ(55, 28))));
    seed(
        "game-promo-check",
        "CHESS_COM",
        "user9",
        "user10",
        1500,
        1500,
        "A00",
        "1-0",
        Map.of(Motif.PROMOTION_WITH_CHECK, List.of(occ(61, 31))));
    seed(
        "game-promo-checkmate",
        "CHESS_COM",
        "user11",
        "user12",
        1600,
        1600,
        "A10",
        "1-0",
        Map.of(Motif.PROMOTION_WITH_CHECKMATE, List.of(occ(65, 33))));
    seed(
        "game-back-rank-mate",
        "LICHESS",
        "user13",
        "user14",
        1800,
        1800,
        "A20",
        "1-0",
        Map.of(Motif.BACK_RANK_MATE, List.of(occ(71, 36))));
    seed(
        "game-smothered-mate",
        "CHESS_COM",
        "user15",
        "user16",
        1900,
        1900,
        "A30",
        "1-0",
        Map.of(Motif.SMOTHERED_MATE, List.of(occ(45, 23))));
    seed(
        "game-zugzwang",
        "CHESS_COM",
        "user17",
        "user18",
        2000,
        2000,
        "A40",
        "1-0",
        Map.of(Motif.ZUGZWANG, List.of(occ(33, 17))));
    seed(
        "game-overloaded",
        "CHESS_COM",
        "user19",
        "user20",
        2100,
        2100,
        "A50",
        "1-0",
        Map.of(Motif.OVERLOADED_PIECE, List.of(occ(25, 13))));

    // ATTACK-derived motifs
    seed(
        "game-fork",
        "CHESS_COM",
        "user21",
        "user22",
        1500,
        1500,
        "B10",
        "1-0",
        Map.of(
            Motif.ATTACK,
            List.of(
                GameFeatures.MotifOccurrence.attack(
                    15, 8, "white", "Fork", "Ng5g6", "Ng6", "rh6", false, false),
                GameFeatures.MotifOccurrence.attack(
                    15, 8, "white", "Fork", "Ng5g6", "Ng6", "ke8", false, false))));
    seed(
        "game-disc-attack",
        "CHESS_COM",
        "user23",
        "user24",
        1500,
        1500,
        "B20",
        "1-0",
        Map.of(
            Motif.ATTACK,
            List.of(
                GameFeatures.MotifOccurrence.attack(
                    59, 30, "white", "Discovered attack", "Kg1g2", "Ra1", "rh1", true, false))));
    seed(
        "game-disc-check",
        "CHESS_COM",
        "user25",
        "user26",
        1500,
        1500,
        "B30",
        "1-0",
        Map.of(
            Motif.ATTACK,
            List.of(
                GameFeatures.MotifOccurrence.attack(
                    15, 8, "white", "Discovered check", "Pf5", "Bg2", "ke8", true, false))));
    seed(
        "game-checkmate",
        "CHESS_COM",
        "user27",
        "user28",
        1500,
        1500,
        "B40",
        "1-0",
        Map.of(
            Motif.ATTACK,
            List.of(
                GameFeatures.MotifOccurrence.attack(
                    107, 54, "white", "Checkmate", "Ra5", "Ra5", "ka8", false, true))));
    seed(
        "game-double-check",
        "CHESS_COM",
        "user29",
        "user30",
        1500,
        1500,
        "B50",
        "1-0",
        Map.of(
            Motif.ATTACK,
            List.of(
                GameFeatures.MotifOccurrence.attack(
                    19, 10, "white", "Double check direct", "Bd3", "Bd3", "ke8", false, false),
                GameFeatures.MotifOccurrence.attack(
                    19, 10, "white", "Double check disc", "Bd3", "Rd1", "ke8", true, false))));

    // PIN + CHECK at consecutive plies (5 and 7) — seq(pin, check) matches since 7 == 5 + 2
    seed(
        "game-pin-check",
        "CHESS_COM",
        "user31",
        "user32",
        2000,
        2000,
        "C20",
        "1-0",
        Map.of(
            Motif.PIN, List.of(occ(5, 3)),
            Motif.CHECK, List.of(occ(7, 4))));

    // Game with no motifs; varied elo/eco for comparison tests
    seed("game-elo-high", "CHESS_COM", "user33", "user34", 2200, 2200, "D10", "1/2-1/2", Map.of());
  }

  private void seed(
      String gameUrl,
      String platform,
      String white,
      String black,
      int whiteElo,
      int blackElo,
      String eco,
      String result,
      Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences) {
    GameFeature game =
        new GameFeature(
            null,
            requestId,
            gameUrl,
            platform,
            white,
            black,
            whiteElo,
            blackElo,
            "blitz",
            eco,
            result,
            Instant.now(),
            30,
            Instant.now(),
            "pgn");
    dao.insert(game);
    if (!occurrences.isEmpty()) {
      dao.insertOccurrences(gameUrl, occurrences);
    }
  }

  /** Stored motif occurrence with no attacker/target fields. */
  private static GameFeatures.MotifOccurrence occ(int ply, int moveNumber) {
    return new GameFeatures.MotifOccurrence(
        ply, moveNumber, "white", "desc", null, null, null, false, false, null);
  }

  // === Contract assertion ===

  /**
   * Asserts that both compilers return the same set of game_urls (order-independent) for the given
   * ChessQL expression.
   */
  private void assertSameResults(String chessql) {
    CompiledQuery sqlCq = sqlCompiler.compile(Parser.parse(chessql));
    CompiledQuery dfCq = dfCompiler.compile(Parser.parse(chessql));

    List<String> sqlUrls =
        dao.query(sqlCq, 100, 0).stream()
            .map(GameFeature::gameUrl)
            .sorted()
            .collect(Collectors.toList());
    List<String> dfUrls =
        dao.query(dfCq, 100, 0).stream()
            .map(GameFeature::gameUrl)
            .sorted()
            .collect(Collectors.toList());

    assertThat(dfUrls)
        .as("DataFusion SQL should match SqlCompiler for: " + chessql)
        .isEqualTo(sqlUrls);
    assertThat(dfCq.parameters()).isEmpty();
  }

  // === Stored motifs ===

  @Test
  public void storedMotif_pin() {
    assertSameResults("motif(pin)");
  }

  @Test
  public void storedMotif_check() {
    assertSameResults("motif(check)");
  }

  @Test
  public void storedMotif_crossPin() {
    assertSameResults("motif(cross_pin)");
  }

  @Test
  public void storedMotif_skewer() {
    assertSameResults("motif(skewer)");
  }

  @Test
  public void storedMotif_promotion() {
    assertSameResults("motif(promotion)");
  }

  @Test
  public void storedMotif_promotionWithCheck() {
    assertSameResults("motif(promotion_with_check)");
  }

  @Test
  public void storedMotif_promotionWithCheckmate() {
    assertSameResults("motif(promotion_with_checkmate)");
  }

  @Test
  public void storedMotif_backRankMate() {
    assertSameResults("motif(back_rank_mate)");
  }

  @Test
  public void storedMotif_smotheredMate() {
    assertSameResults("motif(smothered_mate)");
  }

  @Test
  public void storedMotif_zugzwang() {
    assertSameResults("motif(zugzwang)");
  }

  @Test
  public void storedMotif_overloadedPiece() {
    assertSameResults("motif(overloaded_piece)");
  }

  // === ATTACK-derived motifs ===

  @Test
  public void derivedMotif_fork() {
    assertSameResults("motif(fork)");
  }

  @Test
  public void derivedMotif_discoveredAttack() {
    assertSameResults("motif(discovered_attack)");
  }

  @Test
  public void derivedMotif_discoveredCheck() {
    assertSameResults("motif(discovered_check)");
  }

  @Test
  public void derivedMotif_checkmate() {
    assertSameResults("motif(checkmate)");
  }

  @Test
  public void derivedMotif_doubleCheck() {
    assertSameResults("motif(double_check)");
  }

  // === Comparison expressions ===

  @Test
  public void comparison_numericGte() {
    assertSameResults("white_elo >= 2000");
  }

  @Test
  public void comparison_numericLt() {
    assertSameResults("white_elo < 1600");
  }

  @Test
  public void comparison_numericEq() {
    assertSameResults("white_elo = 2800");
  }

  @Test
  public void comparison_stringEq() {
    assertSameResults("eco = \"B90\"");
  }

  @Test
  public void comparison_stringNe() {
    assertSameResults("eco != \"B90\"");
  }

  @Test
  public void comparison_usernameEq() {
    assertSameResults("white.username = \"hikaru\"");
  }

  @Test
  public void comparison_resultEq() {
    assertSameResults("result = \"1-0\"");
  }

  // === IN expressions ===

  @Test
  public void inExpr_platform() {
    assertSameResults("platform IN [\"CHESS_COM\", \"LICHESS\"]");
  }

  @Test
  public void inExpr_eco() {
    assertSameResults("eco IN [\"B90\", \"C00\", \"C20\"]");
  }

  @Test
  public void inExpr_numericElo() {
    assertSameResults("white_elo IN [1600, 1700, 1800]");
  }

  // === AND / OR / NOT ===

  @Test
  public void and_pinAndCheck() {
    assertSameResults("motif(pin) AND motif(check)");
  }

  @Test
  public void or_pinOrCheck() {
    assertSameResults("motif(pin) OR motif(check)");
  }

  @Test
  public void not_pin() {
    assertSameResults("NOT motif(pin)");
  }

  @Test
  public void and_eloAndMotif() {
    assertSameResults("white_elo >= 2000 AND motif(pin)");
  }

  // === Sequence ===

  @Test
  public void sequence_pinThenCheck() {
    // game-pin-check: PIN at ply 5, CHECK at ply 7 = 5+2 — matches sequence(pin THEN check)
    assertSameResults("sequence(pin THEN check)");
  }

  // === ORDER BY ===

  @Test
  public void orderBy_checkCountAsc() {
    // game-check has 2 check occurrences; game-pin-check has 1.
    // Both compilers should return the same two games (result set equivalence).
    assertSameResults("motif(check) ORDER BY motif_count(check) ASC");
  }

  @Test
  public void orderBy_checkCountDesc() {
    assertSameResults("motif(check) ORDER BY motif_count(check) DESC");
  }
}
