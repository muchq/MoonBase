package com.muchq.games.one_d4.e2e;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chess_com_client.Accuracies;
import com.muchq.games.chess_com_client.PlayedGame;
import com.muchq.games.chess_com_client.PlayerResult;
import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.IndexController;
import com.muchq.games.one_d4.api.IndexRequestValidator;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.IndexRequest;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.db.DataSourceFactory;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.IndexedPeriodStore;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.db.Migration;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.GameReplayer;
import com.muchq.games.one_d4.engine.PgnParser;
import com.muchq.games.one_d4.motifs.AttackDetector;
import com.muchq.games.one_d4.motifs.BackRankMateDetector;
import com.muchq.games.one_d4.motifs.CheckDetector;
import com.muchq.games.one_d4.motifs.CheckmateDetector;
import com.muchq.games.one_d4.motifs.CrossPinDetector;
import com.muchq.games.one_d4.motifs.DiscoveredCheckDetector;
import com.muchq.games.one_d4.motifs.DoubleCheckDetector;
import com.muchq.games.one_d4.motifs.InterferenceDetector;
import com.muchq.games.one_d4.motifs.MotifDetector;
import com.muchq.games.one_d4.motifs.OverloadedPieceDetector;
import com.muchq.games.one_d4.motifs.PinDetector;
import com.muchq.games.one_d4.motifs.PromotionDetector;
import com.muchq.games.one_d4.motifs.PromotionWithCheckDetector;
import com.muchq.games.one_d4.motifs.PromotionWithCheckmateDetector;
import com.muchq.games.one_d4.motifs.SacrificeDetector;
import com.muchq.games.one_d4.motifs.SkewerDetector;
import com.muchq.games.one_d4.motifs.SmotheredMateDetector;
import com.muchq.games.one_d4.motifs.ZugzwangDetector;
import com.muchq.games.one_d4.queue.InMemoryIndexQueue;
import com.muchq.games.one_d4.queue.IndexMessage;
import com.muchq.games.one_d4.queue.IndexQueue;
import com.muchq.games.one_d4.worker.IndexWorker;
import java.time.Duration;
import java.time.Instant;
import java.time.YearMonth;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import javax.sql.DataSource;
import org.junit.Before;
import org.junit.Test;

/**
 * End-to-end motif tests: each test indexes a real game, queries via ChessQL, and verifies that the
 * correct motif occurrence is stored with the expected structured fields.
 *
 * <p>Two games are used:
 *
 * <ul>
 *   <li>King's Gambit (54 moves) — covers PIN, FORK, SKEWER, ATTACK, DISCOVERED_ATTACK, CHECK,
 *       CHECKMATE, PROMOTION, PROMOTION_WITH_CHECK, SACRIFICE, INTERFERENCE, OVERLOADED_PIECE.
 *   <li>Opera Game (17 moves, Morphy 1858) — covers BACK_RANK_MATE (17.Rd8#).
 * </ul>
 *
 * <p>DISCOVERED_CHECK is omitted from e2e tests; it is thoroughly covered by unit tests in
 * DiscoveredCheckDetectorTest.
 */
public class MotifE2ETest {

  private static final String PLAYER = "motif_e2e_player";
  private static final String PLATFORM = "CHESS_COM";
  private static final YearMonth MONTH = YearMonth.of(2024, 7);

  // King's Gambit: _prior vs zapblast, 2024-12-30 (from FullMotifDetectorTest).
  // Contains: PIN, FORK, SKEWER, ATTACK, DISCOVERED_ATTACK, CHECK, CHECKMATE, PROMOTION,
  // PROMOTION_WITH_CHECK, SACRIFICE, INTERFERENCE, OVERLOADED_PIECE.
  private static final String KINGS_GAMBIT_PGN =
      """
      [Event "Live Chess"]
      [Site "Chess.com"]
      [White "_prior"]
      [Black "zapblast"]
      [Result "1-0"]
      [ECO "C30"]

      1. e4 e5 2. f4 d6 3. Nf3 Nc6 4. Bb5 Bd7 5. Nc3 f6 6. f5 Be7 7. Nh4 h5 \
      8. Ng6 Rh6 9. Nd5 Nd4 10. Bxd7+ Qxd7 11. d3 Rh7 12. h4 c6 13. Ngxe7 Nxe7 \
      14. Nxe7 Kxe7 15. Be3 c5 16. g4 hxg4 17. Qxg4 Qa4 18. Bxd4 cxd4 19. Qg6 Rah8 \
      20. a3 Qxc2 21. O-O Rxh4 22. Qxg7+ Ke8 23. Qg6+ Kf8 24. Qxf6+ Ke8 25. Qe6+ Kd8 \
      26. Qxd6+ Kc8 27. Qe6+ Kb8 28. Qxe5+ Ka8 29. Rf2 Rh1+ 30. Kg2 R8h2+ 31. Qxh2 Rxh2+ \
      32. Kxh2 Qxf2+ 33. Kh1 Qxb2 34. Rg1 a6 35. f6 Qf2 36. e5 Qf3+ 37. Kh2 Qf4+ \
      38. Rg3 Qxe5 39. f7 Qh5+ 40. Kg2 Qxf7 41. Rf3 Qa2+ 42. Kg3 Qxa3 43. Kf4 Qf8+ \
      44. Ke4 Qe8+ 45. Kxd4 Qd7+ 46. Ke5 a5 47. d4 a4 48. d5 Qg7+ 49. Ke6 Qg4+ \
      50. Rf5 a3 51. d6 Kb8 52. d7 Qg7 53. d8=Q+ Ka7 54. Ra5# 1-0
      """;

  // Opera Game (Morphy, Paris 1858) for BACK_RANK_MATE:
  // 17.Rd8# — rook delivers back-rank mate to black king on e8.
  private static final String OPERA_GAME_PGN =
      """
      [Event "Opera Game"]
      [White "Morphy"]
      [Black "Duke Karl"]
      [Result "1-0"]
      [ECO "C41"]

      1. e4 e5 2. Nf3 d6 3. d4 Bg4 4. dxe5 Bxf3 5. Qxf3 dxe5 6. Bc4 Nf6 \
      7. Qb3 Qe7 8. Nc3 c6 9. Bg5 b5 10. Nxb5 cxb5 11. Bxb5+ Nbd7 12. O-O-O Rd8 \
      13. Rxd7 Rxd7 14. Rd1 Qe6 15. Bxd7+ Nxd7 16. Qb8+ Nxb8 17. Rd8# 1-0
      """;

  private IndexController controller;
  private IndexQueue queue;
  private IndexWorker worker;
  private GameFeatureStore gameFeatureStore;
  private FakeChessClient fakeChessClient;
  private IndexingRequestStore requestStore;
  private IndexedPeriodStore periodStore;

  @Before
  public void setUp() {
    String jdbcUrl = "jdbc:h2:mem:motife2e_" + System.currentTimeMillis() + ";DB_CLOSE_DELAY=-1";
    DataSource dataSource = DataSourceFactory.create(jdbcUrl, "sa", "");
    Migration migration = new Migration(dataSource, true);
    migration.run();

    requestStore = new com.muchq.games.one_d4.db.IndexingRequestDao(dataSource);
    periodStore = new com.muchq.games.one_d4.db.IndexedPeriodDao(dataSource, true);
    gameFeatureStore = new com.muchq.games.one_d4.db.GameFeatureDao(dataSource, true);

    queue = new InMemoryIndexQueue();
    fakeChessClient = new FakeChessClient();

    List<MotifDetector> detectors =
        List.of(
            new PinDetector(),
            new CrossPinDetector(),
            new SkewerDetector(),
            new AttackDetector(),
            new DiscoveredCheckDetector(),
            new CheckDetector(),
            new CheckmateDetector(),
            new PromotionDetector(),
            new PromotionWithCheckDetector(),
            new PromotionWithCheckmateDetector(),
            new BackRankMateDetector(),
            new SmotheredMateDetector(),
            new SacrificeDetector(),
            new ZugzwangDetector(),
            new DoubleCheckDetector(),
            new InterferenceDetector(),
            new OverloadedPieceDetector());
    FeatureExtractor featureExtractor =
        new FeatureExtractor(new PgnParser(), new GameReplayer(), detectors);
    worker =
        new IndexWorker(
            fakeChessClient, featureExtractor, requestStore, gameFeatureStore, periodStore);
    controller = new IndexController(requestStore, queue, new IndexRequestValidator());
  }

  // === CHECK ===

  @Test
  public void check_motifDetectedWithAttackerAndTarget() {
    String url = indexGame(KINGS_GAMBIT_URL, KINGS_GAMBIT_PGN);
    assertMotifDetected(url, "check");
    List<OccurrenceRow> occs = getOccurrences(url, "check");
    assertThat(occs).isNotEmpty();
    assertThat(occs.get(0).attacker()).isNotNull();
    assertThat(occs.get(0).target()).isNotNull();
  }

  // === CHECKMATE ===

  @Test
  public void checkmate_motifDetectedWithIsMateTrue() {
    String url = indexGame(KINGS_GAMBIT_URL, KINGS_GAMBIT_PGN);
    assertMotifDetected(url, "checkmate");
    List<OccurrenceRow> occs = getOccurrences(url, "checkmate");
    assertThat(occs).hasSize(1);
    assertThat(occs.get(0).isMate()).isTrue();
    assertThat(occs.get(0).moveNumber()).isEqualTo(54);
    assertThat(occs.get(0).side()).isEqualTo("white");
    assertThat(occs.get(0).attacker()).isNotNull();
    assertThat(occs.get(0).target()).isNotNull();
  }

  // === ATTACK ===

  @Test
  public void attack_motifDetected() {
    String url = indexGame(KINGS_GAMBIT_URL, KINGS_GAMBIT_PGN);
    assertThat(getOccurrences(url, "attack")).isNotEmpty();
  }

  // === DISCOVERED_ATTACK ===

  @Test
  public void discoveredAttack_motifDetected() {
    // Derived from ATTACK occurrences where isDiscovered=true.
    // King's Gambit has discovered attacks at moves 9, 11, 16, 30, 44.
    String url = indexGame(KINGS_GAMBIT_URL, KINGS_GAMBIT_PGN);
    assertMotifDetected(url, "discovered_attack");
    // Attack occurrences with isDiscovered=true should be present
    List<OccurrenceRow> attackOccs = getOccurrences(url, "attack");
    assertThat(attackOccs.stream().anyMatch(OccurrenceRow::isDiscovered)).isTrue();
  }

  // === FORK ===

  @Test
  public void fork_motifDetectedWithAttackerAndTarget() {
    String url = indexGame(KINGS_GAMBIT_URL, KINGS_GAMBIT_PGN);
    assertMotifDetected(url, "fork");
    List<OccurrenceRow> occs = getOccurrences(url, "fork");
    assertThat(occs).isNotEmpty();
    assertThat(occs.get(0).attacker()).isNotNull();
    assertThat(occs.get(0).target()).isNotNull();
    assertThat(occs.get(0).pinType()).isNull();
  }

  // === PIN ===

  @Test
  public void pin_motifDetectedWithAttackerTargetAndPinType() {
    String url = indexGame(KINGS_GAMBIT_URL, KINGS_GAMBIT_PGN);
    assertMotifDetected(url, "pin");
    List<OccurrenceRow> occs = getOccurrences(url, "pin");
    assertThat(occs).isNotEmpty();
    // All pin occurrences have attacker, target, and pinType populated
    assertThat(occs).allMatch(o -> o.attacker() != null, "attacker non-null");
    assertThat(occs).allMatch(o -> o.target() != null, "target non-null");
    assertThat(occs).allMatch(o -> o.pinType() != null, "pinType non-null");
    assertThat(occs)
        .allMatch(
            o -> "ABSOLUTE".equals(o.pinType()) || "RELATIVE".equals(o.pinType()), "pinType valid");
    // King's Gambit move 4: Bb5 pins Nc6 to Ke8 (absolute pin)
    assertThat(occs)
        .anyMatch(
            o -> o.moveNumber() == 4 && "white".equals(o.side()) && "ABSOLUTE".equals(o.pinType()));
  }

  // === SKEWER ===

  @Test
  public void skewer_motifDetectedWithAttackerAndTarget() {
    String url = indexGame(KINGS_GAMBIT_URL, KINGS_GAMBIT_PGN);
    assertMotifDetected(url, "skewer");
    List<OccurrenceRow> occs = getOccurrences(url, "skewer");
    assertThat(occs).isNotEmpty();
    assertThat(occs.get(0).attacker()).isNotNull();
    assertThat(occs.get(0).target()).isNotNull();
  }

  // === SACRIFICE ===

  @Test
  public void sacrifice_motifDetected() {
    String url = indexGame(KINGS_GAMBIT_URL, KINGS_GAMBIT_PGN);
    assertMotifDetected(url, "sacrifice");
    assertThat(getOccurrences(url, "sacrifice")).isNotEmpty();
  }

  // === PROMOTION ===

  @Test
  public void promotion_motifDetectedAtMove53() {
    String url = indexGame(KINGS_GAMBIT_URL, KINGS_GAMBIT_PGN);
    assertMotifDetected(url, "promotion");
    List<OccurrenceRow> occs = getOccurrences(url, "promotion");
    assertThat(occs).hasSize(1);
    assertThat(occs.get(0).moveNumber()).isEqualTo(53);
    assertThat(occs.get(0).side()).isEqualTo("white");
  }

  // === PROMOTION_WITH_CHECK ===

  @Test
  public void promotionWithCheck_motifDetectedAtMove53() {
    String url = indexGame(KINGS_GAMBIT_URL, KINGS_GAMBIT_PGN);
    assertMotifDetected(url, "promotion_with_check");
    List<OccurrenceRow> occs = getOccurrences(url, "promotion_with_check");
    assertThat(occs).hasSize(1);
    assertThat(occs.get(0).moveNumber()).isEqualTo(53);
    assertThat(occs.get(0).attacker()).isNotNull();
    assertThat(occs.get(0).target()).isNotNull();
  }

  // === INTERFERENCE ===

  @Test
  public void interference_motifDetected() {
    String url = indexGame(KINGS_GAMBIT_URL, KINGS_GAMBIT_PGN);
    assertMotifDetected(url, "interference");
    assertThat(getOccurrences(url, "interference")).isNotEmpty();
  }

  // === OVERLOADED_PIECE ===

  @Test
  public void overloadedPiece_motifDetected() {
    String url = indexGame(KINGS_GAMBIT_URL, KINGS_GAMBIT_PGN);
    assertMotifDetected(url, "overloaded_piece");
    assertThat(getOccurrences(url, "overloaded_piece")).isNotEmpty();
  }

  // === BACK_RANK_MATE ===

  @Test
  public void backRankMate_motifDetectedWithIsMateTrue() {
    // Opera Game: 17.Rd8# — white rook delivers back-rank mate to black king on e8.
    String url = indexGame("https://chess.com/game/opera-1858", OPERA_GAME_PGN);
    assertMotifDetected(url, "back_rank_mate");
    List<OccurrenceRow> occs = getOccurrences(url, "back_rank_mate");
    assertThat(occs).hasSize(1);
    assertThat(occs.get(0).moveNumber()).isEqualTo(17);
    assertThat(occs.get(0).side()).isEqualTo("white");
    assertThat(occs.get(0).isMate()).isTrue();
    assertThat(occs.get(0).attacker()).isEqualTo("Rd8");
    assertThat(occs.get(0).target()).isEqualTo("ke8");
  }

  // ===== Helpers =====

  private static final String KINGS_GAMBIT_URL = "https://chess.com/game/kings-gambit-1";

  /** Indexes a single game and returns its URL. */
  private String indexGame(String url, String pgn) {
    fakeChessClient.setGames(PLAYER, MONTH, List.of(playedGame(url, pgn)));
    IndexRequest request = new IndexRequest(PLAYER, PLATFORM, "2024-07", "2024-07");
    controller.createIndex(request);
    processQueueUntilIdle();
    return url;
  }

  /** Asserts that a game with the given URL is returned by the named motif query. */
  private void assertMotifDetected(String expectedUrl, String motifName) {
    CompiledQuery compiled = new SqlCompiler().compile(Parser.parse("motif(" + motifName + ")"));
    List<GameFeature> results = gameFeatureStore.query(compiled, 10, 0);
    assertThat(results).hasSize(1);
    assertThat(results.get(0).gameUrl()).isEqualTo(expectedUrl);
  }

  /** Returns the occurrences for a given game URL and motif name (lowercase). */
  private List<OccurrenceRow> getOccurrences(String gameUrl, String motifName) {
    Map<String, Map<String, List<OccurrenceRow>>> all =
        gameFeatureStore.queryOccurrences(List.of(gameUrl));
    return all.getOrDefault(gameUrl, Map.of()).getOrDefault(motifName, List.of());
  }

  private static PlayedGame playedGame(String url, String pgn) {
    return new PlayedGame(
        url,
        pgn,
        Instant.EPOCH,
        true,
        new Accuracies(90.0, 85.0),
        "",
        "uuid-" + url.hashCode(),
        "",
        "",
        "blitz",
        "chess",
        new PlayerResult(1500, "win", "https://chess.com/w", "White", "uuid-w"),
        new PlayerResult(1500, "loss", "https://chess.com/b", "Black", "uuid-b"),
        "C30");
  }

  private void processQueueUntilIdle() {
    int maxIterations = 100;
    for (int i = 0; i < maxIterations; i++) {
      Optional<IndexMessage> message = queue.poll(Duration.ofMillis(50));
      if (message.isEmpty()) {
        return;
      }
      worker.process(message.get());
    }
    throw new AssertionError("Queue did not drain within " + maxIterations + " iterations");
  }
}
