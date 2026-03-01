package com.muchq.games.one_d4.e2e;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chess_com_client.Accuracies;
import com.muchq.games.chess_com_client.PlayedGame;
import com.muchq.games.chess_com_client.PlayerResult;
import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.AdminController;
import com.muchq.games.one_d4.api.IndexController;
import com.muchq.games.one_d4.api.IndexRequestValidator;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.IndexRequest;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.api.dto.ReanalysisResponse;
import com.muchq.games.one_d4.db.DataSourceFactory;
import com.muchq.games.one_d4.db.GameFeatureDao;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.IndexedPeriodDao;
import com.muchq.games.one_d4.db.IndexedPeriodStore;
import com.muchq.games.one_d4.db.IndexingRequestDao;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.db.Migration;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.GameReplayer;
import com.muchq.games.one_d4.engine.PgnParser;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.motifs.AttackDetector;
import com.muchq.games.one_d4.motifs.BackRankMateDetector;
import com.muchq.games.one_d4.motifs.CheckDetector;
import com.muchq.games.one_d4.motifs.CrossPinDetector;
import com.muchq.games.one_d4.motifs.MotifDetector;
import com.muchq.games.one_d4.motifs.OverloadedPieceDetector;
import com.muchq.games.one_d4.motifs.PinDetector;
import com.muchq.games.one_d4.motifs.PromotionDetector;
import com.muchq.games.one_d4.motifs.PromotionWithCheckDetector;
import com.muchq.games.one_d4.motifs.PromotionWithCheckmateDetector;
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
 * End-to-end tests for the POST /admin/reanalyze endpoint. Each test indexes one or more games,
 * then calls AdminController.reanalyze() and verifies that motif columns and occurrences are
 * correctly refreshed.
 */
public class ReanalysisE2ETest {

  private static final String PLAYER = "reanalysis_e2e_player";
  private static final String PLATFORM = "CHESS_COM";
  private static final YearMonth MONTH = YearMonth.of(2024, 7);

  // King's Gambit game used across motif tests — has PIN, CHECK, PROMOTION, etc.
  private static final String KINGS_GAMBIT_URL = "https://chess.com/game/kings-gambit-reanalysis";
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

  private IndexController controller;
  private IndexQueue queue;
  private IndexWorker worker;
  private GameFeatureStore gameFeatureStore;
  private AdminController adminController;
  private FakeChessClient fakeChessClient;
  private IndexingRequestStore requestStore;
  private IndexedPeriodStore periodStore;
  private FeatureExtractor featureExtractor;

  @Before
  public void setUp() {
    String jdbcUrl =
        "jdbc:h2:mem:reanalysis_e2e_" + System.currentTimeMillis() + ";DB_CLOSE_DELAY=-1";
    DataSource dataSource = DataSourceFactory.create(jdbcUrl, "sa", "");
    Migration migration = new Migration(dataSource, true);
    migration.run();

    requestStore = new IndexingRequestDao(dataSource);
    periodStore = new IndexedPeriodDao(dataSource, true);
    gameFeatureStore = new GameFeatureDao(dataSource, true);

    queue = new InMemoryIndexQueue();
    fakeChessClient = new FakeChessClient();

    List<MotifDetector> detectors =
        List.of(
            new PinDetector(),
            new CrossPinDetector(),
            new SkewerDetector(),
            new AttackDetector(),
            new CheckDetector(),
            new PromotionDetector(),
            new PromotionWithCheckDetector(),
            new PromotionWithCheckmateDetector(),
            new BackRankMateDetector(),
            new SmotheredMateDetector(),
            new ZugzwangDetector(),
            new OverloadedPieceDetector());
    featureExtractor = new FeatureExtractor(new PgnParser(), new GameReplayer(), detectors);

    worker =
        new IndexWorker(
            fakeChessClient, featureExtractor, requestStore, gameFeatureStore, periodStore);
    controller = new IndexController(requestStore, queue, new IndexRequestValidator());
    adminController = new AdminController(gameFeatureStore, featureExtractor);
  }

  // === Basic reanalysis ===

  @Test
  public void reanalyze_noGames_returnsZeroCounts() {
    ReanalysisResponse response = adminController.reanalyze();

    assertThat(response.gamesProcessed()).isEqualTo(0);
    assertThat(response.gamesFailed()).isEqualTo(0);
  }

  @Test
  public void reanalyze_kingsGambitGame_processesSingleGame() {
    indexGame(KINGS_GAMBIT_URL, KINGS_GAMBIT_PGN);

    ReanalysisResponse response = adminController.reanalyze();

    assertThat(response.gamesProcessed()).isEqualTo(1);
    assertThat(response.gamesFailed()).isEqualTo(0);
  }

  @Test
  public void reanalyze_kingsGambitGame_motifColumnsPreserved() {
    // Index the game so motif columns are set.
    indexGame(KINGS_GAMBIT_URL, KINGS_GAMBIT_PGN);
    assertMotifDetected(KINGS_GAMBIT_URL, "pin");
    assertMotifDetected(KINGS_GAMBIT_URL, "check");
    assertMotifDetected(KINGS_GAMBIT_URL, "promotion");

    // Reanalysis should be idempotent — motif columns stay set.
    ReanalysisResponse response = adminController.reanalyze();

    assertThat(response.gamesProcessed()).isEqualTo(1);
    assertMotifDetected(KINGS_GAMBIT_URL, "pin");
    assertMotifDetected(KINGS_GAMBIT_URL, "check");
    assertMotifDetected(KINGS_GAMBIT_URL, "promotion");
  }

  // === Occurrence replacement ===

  @Test
  public void reanalyze_replacesOccurrences_staleOccurrenceIsRemoved() {
    indexGame(KINGS_GAMBIT_URL, KINGS_GAMBIT_PGN);

    // Insert a fake extra pin occurrence to simulate stale data.
    GameFeatures.MotifOccurrence fakeOcc =
        new GameFeatures.MotifOccurrence(
            999, 500, "white", "Fake stale pin", null, "Qa1", "kb8", false, false, "ABSOLUTE");
    gameFeatureStore.insertOccurrences(KINGS_GAMBIT_URL, Map.of(Motif.PIN, List.of(fakeOcc)));

    // Before reanalysis: the fake occurrence at ply 999 is in the DB.
    List<OccurrenceRow> pinsBefore = getOccurrences(KINGS_GAMBIT_URL, "pin");
    assertThat(pinsBefore).anyMatch(o -> o.moveNumber() == 500);

    // Reanalyze: deletes all occurrences then re-inserts from detectors.
    adminController.reanalyze();

    // After reanalysis: fake occurrence is gone; real pins still present.
    List<OccurrenceRow> pinsAfter = getOccurrences(KINGS_GAMBIT_URL, "pin");
    assertThat(pinsAfter).noneMatch(o -> o.moveNumber() == 500);
    assertThat(pinsAfter).isNotEmpty();
  }

  @Test
  public void reanalyze_withNoDetectors_clearsMotifColumnsAndOccurrences() {
    // Index the game with all detectors → pin detected.
    indexGame(KINGS_GAMBIT_URL, KINGS_GAMBIT_PGN);
    assertMotifDetected(KINGS_GAMBIT_URL, "pin");

    // Build an AdminController with NO detectors — re-analysis will find nothing.
    FeatureExtractor emptyExtractor =
        new FeatureExtractor(new PgnParser(), new GameReplayer(), List.of());
    AdminController emptyAdminController = new AdminController(gameFeatureStore, emptyExtractor);

    ReanalysisResponse response = emptyAdminController.reanalyze();

    assertThat(response.gamesProcessed()).isEqualTo(1);

    // Motif columns should now be false (no detectors → no motifs found).
    CompiledQuery pinQuery = new SqlCompiler().compile(Parser.parse("motif(pin)"));
    List<GameFeature> pinned = gameFeatureStore.query(pinQuery, 10, 0);
    assertThat(pinned).isEmpty();

    // Occurrences should be empty.
    List<OccurrenceRow> occs = getOccurrences(KINGS_GAMBIT_URL, "pin");
    assertThat(occs).isEmpty();
  }

  @Test
  public void reanalyze_multipleGames_allProcessed() {
    // Index both games in a single request so they land in the same batch.
    String url2 = "https://chess.com/game/opera-reanalysis";
    String operaPgn =
        """
        [Event "Opera Game"]
        [White "Morphy"]
        [Black "Duke Karl"]
        [Result "1-0"]

        1. e4 e5 2. Nf3 d6 3. d4 Bg4 4. dxe5 Bxf3 5. Qxf3 dxe5 6. Bc4 Nf6 \
        7. Qb3 Qe7 8. Nc3 c6 9. Bg5 b5 10. Nxb5 cxb5 11. Bxb5+ Nbd7 12. O-O-O Rd8 \
        13. Rxd7 Rxd7 14. Rd1 Qe6 15. Bxd7+ Nxd7 16. Qb8+ Nxb8 17. Rd8# 1-0
        """;

    indexGames(List.of(playedGame(KINGS_GAMBIT_URL, KINGS_GAMBIT_PGN), playedGame(url2, operaPgn)));

    ReanalysisResponse response = adminController.reanalyze();

    assertThat(response.gamesProcessed()).isEqualTo(2);
    assertThat(response.gamesFailed()).isEqualTo(0);
    assertMotifDetected(KINGS_GAMBIT_URL, "pin");
    assertMotifDetected(url2, "back_rank_mate");
  }

  // ===== Helpers =====

  private String indexGame(String url, String pgn) {
    fakeChessClient.setGames(PLAYER, MONTH, List.of(playedGame(url, pgn)));
    IndexRequest request = new IndexRequest(PLAYER, PLATFORM, "2024-07", "2024-07", false);
    controller.createIndex(request);
    processQueueUntilIdle();
    return url;
  }

  /** Indexes multiple games in a single indexing request (same player+month batch). */
  private void indexGames(List<PlayedGame> games) {
    fakeChessClient.setGames(PLAYER, MONTH, games);
    IndexRequest request = new IndexRequest(PLAYER, PLATFORM, "2024-07", "2024-07", false);
    controller.createIndex(request);
    processQueueUntilIdle();
  }

  private void assertMotifDetected(String expectedUrl, String motifName) {
    CompiledQuery compiled = new SqlCompiler().compile(Parser.parse("motif(" + motifName + ")"));
    List<GameFeature> results = gameFeatureStore.query(compiled, 10, 0);
    assertThat(results).anyMatch(r -> r.gameUrl().equals(expectedUrl));
  }

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
