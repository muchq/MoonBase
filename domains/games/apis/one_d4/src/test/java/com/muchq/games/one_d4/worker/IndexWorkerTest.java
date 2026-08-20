package com.muchq.games.one_d4.worker;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.games.chess_com_client.Accuracies;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.GamesResponse;
import com.muchq.games.chess_com_client.PlayedGame;
import com.muchq.games.chess_com_client.Player;
import com.muchq.games.chess_com_client.PlayerResult;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.GameFeatureStore.GameOpening;
import com.muchq.games.one_d4.db.IndexedPeriodStore;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.GameReplayer;
import com.muchq.games.one_d4.engine.PgnParser;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.motifs.AttackDetector;
import com.muchq.games.one_d4.motifs.CheckDetector;
import com.muchq.games.one_d4.motifs.CrossPinDetector;
import com.muchq.games.one_d4.motifs.MotifDetector;
import com.muchq.games.one_d4.motifs.PinDetector;
import com.muchq.games.one_d4.motifs.SkewerDetector;
import com.muchq.games.one_d4.queue.IndexMessage;
import com.muchq.platform.yodel.CustomMetrics;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

public class IndexWorkerTest {

  private static final UUID REQUEST_ID = UUID.randomUUID();
  private static final String PLAYER = "testplayer";
  private static final String PLATFORM = "CHESS_COM";

  private StubChessClient stubChessClient;
  private RecordingRequestStore requestStore;
  private StubPeriodStore periodStore;
  private IndexWorker worker;
  private FeatureExtractor featureExtractor;
  private ExecutorService extractionExecutor;

  @BeforeEach
  public void setUp() {
    stubChessClient = new StubChessClient();
    requestStore = new RecordingRequestStore();
    periodStore = new StubPeriodStore();
    extractionExecutor = Executors.newFixedThreadPool(4);
    List<MotifDetector> detectors =
        List.of(
            new PinDetector(), new CrossPinDetector(), new SkewerDetector(), new AttackDetector());
    featureExtractor = new FeatureExtractor(new PgnParser(), new GameReplayer(), detectors);
    worker =
        new IndexWorker(
            stubChessClient,
            featureExtractor,
            requestStore,
            new NoOpGameFeatureStore(),
            periodStore,
            extractionExecutor);
  }

  @AfterEach
  public void tearDown() {
    extractionExecutor.shutdownNow();
  }

  @Test
  public void process_skipsFetchWhenPeriodIsCached() {
    periodStore.setCachedPeriod(
        PLAYER,
        PLATFORM,
        "2024-01",
        new IndexedPeriodStore.IndexedPeriod(
            PLAYER, PLATFORM, "2024-01", Instant.EPOCH, true, 7, false));
    IndexMessage message =
        new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-02", false);

    worker.process(message);

    assertThat(stubChessClient.getFetchCalls()).containsExactly(java.time.YearMonth.of(2024, 2));
    assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
    assertThat(requestStore.getLastGamesIndexed()).isEqualTo(7);
  }

  /**
   * An empty archive is HTTP 200 with {@code {"games":[]}}. The month was still indexed — the
   * answer is "no games" — so it has to leave a period row behind. Without one, the month is
   * indistinguishable from one retention has swept, and {@code DataAvailabilityResolver} reports a
   * request as PARTIAL or EXPIRED the moment it completes. It also means the empty month is
   * refetched forever, because the period cache only hits on a row that exists.
   */
  @Test
  public void process_emptyGamesList_stillRecordsAnEmptyPeriod() {
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(playedGame("https://chess.com/game/jan-1", MINIMAL_PGN, "blitz")));
    stubChessClient.setResponse(java.time.YearMonth.of(2024, 2), List.of());

    worker.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-02", false));

    assertThat(periodStore.getUpserts())
        .extracting(StubPeriodStore.Upsert::month)
        .containsExactly("2024-01", "2024-02");
    assertThat(periodStore.getUpserts())
        .filteredOn(u -> u.month().equals("2024-02"))
        .singleElement()
        .satisfies(
            u -> {
              assertThat(u.gamesCount()).isZero();
              // Both months are over, so both are complete and cacheable.
              assertThat(u.isComplete()).isTrue();
            });
    assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
    assertThat(requestStore.getLastGamesIndexed()).isEqualTo(1);
  }

  /**
   * ChessClient maps a 404 to {@code Optional.empty()}. That is not an empty month — empty months
   * are a 200 with an empty games list — so the request must fail and be retried rather than
   * COMPLETE with zero games (#1360).
   */
  @Test
  public void a404OnAnArchiveChessComListsIsAnErrorNotAnEmptyMonth() {
    stubChessClient.setNotFound(java.time.YearMonth.of(2024, 1));

    worker.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false));

    assertThat(requestStore.getLastStatus()).isEqualTo("FAILED");
    assertThat(periodStore.getUpserts()).isEmpty();
  }

  @Test
  public void metrics_archive404IsCountedAsErrorNotEmptyMonth() {
    stubChessClient.setNotFound(java.time.YearMonth.of(2024, 1));
    IndexWorker w = meteredWorker();

    w.process(oneMonth());

    assertThat(counter(IndexWorker.ARCHIVE_FETCHES, Map.of("result", "error"))).isEqualTo(1);
    assertThat(counter(IndexWorker.ARCHIVE_FETCHES, Map.of("result", "no_archive"))).isZero();
    assertThat(counter(IndexWorker.MONTHS, Map.of("result", "empty"))).isZero();
    assertThat(counter(IndexWorker.RUNS, Map.of("outcome", "failed"))).isEqualTo(1);
    assertThat(counter(IndexWorker.RUNS, Map.of("outcome", "completed"))).isZero();
  }

  /** Negative twin of {@link #a404OnAnArchiveChessComListsIsAnErrorNotAnEmptyMonth}. */
  @Test
  public void a200WithEmptyGamesStillRecordsAnEmptyMonthAndCompletes() {
    stubChessClient.setResponse(java.time.YearMonth.of(2024, 1), List.of());

    worker.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false));

    assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
    assertThat(requestStore.getLastGamesIndexed()).isEqualTo(0);
    assertThat(periodStore.getUpserts())
        .singleElement()
        .satisfies(
            u -> {
              assertThat(u.month()).isEqualTo("2024-01");
              assertThat(u.gamesCount()).isZero();
            });
  }

  @Test
  public void process_fetchesWhenNoCachedPeriod() {
    IndexMessage message =
        new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false);

    worker.process(message);

    assertThat(stubChessClient.getFetchCalls()).containsExactly(java.time.YearMonth.of(2024, 1));
    assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
  }

  @Test
  public void process_skipsFetchWhenMiddleMonthIsCached() {
    periodStore.setCachedPeriod(
        PLAYER,
        PLATFORM,
        "2024-02",
        new IndexedPeriodStore.IndexedPeriod(
            PLAYER, PLATFORM, "2024-02", Instant.EPOCH, true, 5, false));
    IndexMessage message =
        new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-03", false);

    worker.process(message);

    assertThat(stubChessClient.getFetchCalls())
        .containsExactly(java.time.YearMonth.of(2024, 1), java.time.YearMonth.of(2024, 3));
    assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
    assertThat(requestStore.getLastGamesIndexed()).isEqualTo(5);
  }

  @Test
  public void process_whenGameHasMotifs_callsInsertOccurrencesWithOccurrences() {
    // PGN with checkmate (Qxf7#) so CheckDetector fires on last move
    String gameUrl = "https://chess.com/game/with-check";
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1), List.of(playedGame(gameUrl, SCHOLARS_MATE_PGN, "blitz")));
    RecordingGameFeatureStore recordingStore = new RecordingGameFeatureStore();
    List<MotifDetector> detectors =
        List.of(
            new CheckDetector(),
            new PinDetector(),
            new CrossPinDetector(),
            new SkewerDetector(),
            new AttackDetector());
    FeatureExtractor featureExtractor =
        new FeatureExtractor(new PgnParser(), new GameReplayer(), detectors);
    IndexWorker workerWithRecording =
        new IndexWorker(
            stubChessClient,
            featureExtractor,
            requestStore,
            recordingStore,
            periodStore,
            extractionExecutor);

    IndexMessage message =
        new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false);
    workerWithRecording.process(message);

    Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> allOccurrences =
        recordingStore.getAllInsertedOccurrences();
    assertThat(allOccurrences).containsKey(gameUrl);
    Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences = allOccurrences.get(gameUrl);
    assertThat(occurrences).containsKey(Motif.CHECK);
    assertThat(occurrences.get(Motif.CHECK)).isNotEmpty();
    assertThat(occurrences.get(Motif.CHECK).get(0).moveNumber()).isPositive();
    assertThat(occurrences.get(Motif.CHECK).get(0).description()).isNotBlank();
  }

  /**
   * Where NULL elos come from, pinned at the source: chess.com can omit a side's player-result
   * record from the archive entirely, and the worker persists that side's username, elo, and title
   * as NULL — it neither fails the game nor invents a zero rating. These are the rows the aggregate
   * NULL bucket counts, and the reason the docs attribute NULL elos to omitted rating data rather
   * than to schema history.
   */
  @Test
  public void process_gameMissingASidesResultIndexesWithNullUsernameEloAndTitle() {
    String gameUrl = "https://chess.com/game/absent-side";
    PlayedGame missingBlack =
        new PlayedGame(
            gameUrl,
            MINIMAL_PGN,
            Instant.EPOCH,
            true,
            new Accuracies(90.0, 85.0),
            "",
            "uuid-absent",
            "",
            "",
            "blitz",
            "chess",
            new PlayerResult(1500, "win", "https://chess.com/w", "White", "uuid-w"),
            null,
            "C20");
    stubChessClient.setResponse(java.time.YearMonth.of(2024, 1), List.of(missingBlack));
    RecordingGameFeatureStore store = new RecordingGameFeatureStore();
    IndexWorker w =
        new IndexWorker(
            stubChessClient,
            featureExtractor,
            requestStore,
            store,
            periodStore,
            extractionExecutor);

    w.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false));

    assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
    assertThat(store.getInsertedFeatures()).hasSize(1);
    GameFeature feature = store.getInsertedFeatures().get(0);
    assertThat(feature.whiteElo()).isEqualTo(1500);
    assertThat(feature.blackUsername()).isNull();
    assertThat(feature.blackElo()).isNull();
    assertThat(feature.blackTitle()).isNull();
  }

  @Test
  public void process_bulletGamesNotSkippedWhenExcludeBulletFalse() {
    String gameUrl = "https://chess.com/game/bullet-keep";
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1), List.of(playedGame(gameUrl, MINIMAL_PGN, "bullet")));
    RecordingGameFeatureStore recordingStore = new RecordingGameFeatureStore();
    IndexWorker w =
        new IndexWorker(
            stubChessClient,
            featureExtractor,
            requestStore,
            recordingStore,
            periodStore,
            extractionExecutor);

    w.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false));

    assertThat(recordingStore.getInsertCount()).isEqualTo(1);
    assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
  }

  @Test
  public void process_onUnhandledException_storesOpaqueErrorMessage() {
    stubChessClient.setThrowOnFetch(
        new RuntimeException("MERGE INTO indexed_periods ... SQL details"));
    IndexMessage message =
        new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false);

    worker.process(message);

    assertThat(requestStore.getLastStatus()).isEqualTo("FAILED");
    assertThat(requestStore.getLastErrorMessage())
        .doesNotContain("MERGE")
        .doesNotContain("SQL")
        .isEqualTo("Indexing failed due to an internal error");
  }

  @Test
  @Timeout(value = 5, unit = TimeUnit.SECONDS)
  public void process_runsExtractionsConcurrently_acrossPoolThreads() throws Exception {
    // Two games in one month. Each extract() decrements a 2-latch then awaits it.
    // If extract() runs sequentially, the second call never starts and the latch
    // never reaches zero -> the test times out. With a pool size >= 2, both
    // games are in flight at once and the latch releases immediately.
    java.util.concurrent.CountDownLatch latch = new java.util.concurrent.CountDownLatch(2);
    FeatureExtractor latchExtractor =
        new FeatureExtractor(new PgnParser(), new GameReplayer(), List.of()) {
          @Override
          public GameFeatures extract(String pgn) {
            latch.countDown();
            try {
              // No timeout: if the loop is sequential the second game never starts,
              // so the latch never reaches 0 and this blocks forever -> JUnit timeout fires.
              latch.await();
            } catch (InterruptedException e) {
              Thread.currentThread().interrupt();
              throw new RuntimeException(e);
            }
            return new GameFeatures(java.util.EnumSet.noneOf(Motif.class), 0, java.util.Map.of());
          }
        };

    ExecutorService pool = Executors.newFixedThreadPool(2);
    try {
      RecordingGameFeatureStore store = new RecordingGameFeatureStore();
      IndexWorker concurrentWorker =
          new IndexWorker(stubChessClient, latchExtractor, requestStore, store, periodStore, pool);
      stubChessClient.setResponse(
          java.time.YearMonth.of(2024, 1),
          List.of(
              playedGame("https://chess.com/g/1", MINIMAL_PGN, "blitz"),
              playedGame("https://chess.com/g/2", MINIMAL_PGN, "blitz")));

      concurrentWorker.process(
          new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false));

      assertThat(store.getInsertCount()).isEqualTo(2);
      assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
    } finally {
      pool.shutdownNow();
    }
  }

  @Test
  public void process_bulletGamesSkippedWhenExcludeBulletTrue() {
    String gameUrl = "https://chess.com/game/bullet-skip";
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1), List.of(playedGame(gameUrl, MINIMAL_PGN, "bullet")));
    RecordingGameFeatureStore recordingStore = new RecordingGameFeatureStore();
    IndexWorker w =
        new IndexWorker(
            stubChessClient,
            featureExtractor,
            requestStore,
            recordingStore,
            periodStore,
            extractionExecutor);

    w.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", true));

    assertThat(recordingStore.getInsertCount()).isEqualTo(0);
    assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
  }

  @Test
  public void process_oneFailingExtraction_doesNotPreventOthers() {
    String poisonUrl = "https://chess.com/g/poison";
    FeatureExtractor selectivelyFailing =
        new FeatureExtractor(new PgnParser(), new GameReplayer(), List.of()) {
          @Override
          public GameFeatures extract(String pgn) {
            if (pgn.contains("POISON")) {
              throw new RuntimeException("boom");
            }
            return new GameFeatures(java.util.EnumSet.noneOf(Motif.class), 0, java.util.Map.of());
          }
        };

    String poisonPgn =
        """
        [Event "POISON"]
        [Site "Chess.com"]
        [White "W"]
        [Black "B"]
        [Result "1-0"]
        [ECO "C20"]

        1. e4 e5 1-0
        """;

    RecordingGameFeatureStore store = new RecordingGameFeatureStore();
    IndexWorker w =
        new IndexWorker(
            stubChessClient,
            selectivelyFailing,
            requestStore,
            store,
            periodStore,
            extractionExecutor);
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(
            playedGame("https://chess.com/g/ok1", MINIMAL_PGN, "blitz"),
            playedGame(poisonUrl, poisonPgn, "blitz"),
            playedGame("https://chess.com/g/ok2", MINIMAL_PGN, "blitz")));

    w.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false));

    assertThat(store.getInsertCount()).isEqualTo(2);
    assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
    assertThat(requestStore.getLastGamesIndexed()).isEqualTo(2);
  }

  @Test
  public void process_allGamesLandInBatch_regardlessOfOrder() {
    List<String> urls =
        List.of(
            "https://chess.com/g/a",
            "https://chess.com/g/b",
            "https://chess.com/g/c",
            "https://chess.com/g/d");
    List<PlayedGame> games = new ArrayList<>();
    for (String u : urls) {
      games.add(playedGame(u, MINIMAL_PGN, "blitz"));
    }
    stubChessClient.setResponse(java.time.YearMonth.of(2024, 1), games);

    RecordingGameFeatureStore store = new RecordingGameFeatureStore();
    IndexWorker w =
        new IndexWorker(
            stubChessClient,
            featureExtractor,
            requestStore,
            store,
            periodStore,
            extractionExecutor);

    w.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false));

    assertThat(store.getInsertedUrls()).containsExactlyInAnyOrderElementsOf(urls);
    assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
  }

  @Test
  public void process_enrichesTitlesAndOpeningColumns() {
    stubChessClient.setTitle("hikaru", "GM");
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(
            playedGame(
                "https://chess.com/g/opening",
                MINIMAL_PGN,
                "blitz",
                "Hikaru",
                "someuser",
                "https://www.chess.com/openings/Caro-Kann-Defense-Two-Knights-Attack")));
    RecordingGameFeatureStore store = new RecordingGameFeatureStore();
    IndexWorker w =
        new IndexWorker(
            stubChessClient,
            featureExtractor,
            requestStore,
            store,
            periodStore,
            extractionExecutor);

    w.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false));

    assertThat(store.getInsertedFeatures()).hasSize(1);
    GameFeature feature = store.getInsertedFeatures().get(0);
    assertThat(feature.whiteTitle()).isEqualTo("GM");
    assertThat(feature.blackTitle()).isNull();
    assertThat(feature.eco()).isEqualTo("C20");
    assertThat(feature.openingName()).isEqualTo("Caro Kann Defense Two Knights Attack");
    assertThat(feature.openingFamily()).isEqualTo("Caro Kann Defense");
  }

  @Test
  public void process_fetchesEachDistinctPlayerProfileOnce() {
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(
            playedGame("https://chess.com/g/1", MINIMAL_PGN, "blitz", "Hikaru", "opp1", "C20"),
            playedGame("https://chess.com/g/2", MINIMAL_PGN, "blitz", "opp1", "Hikaru", "C20")));
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 2),
        List.of(
            playedGame("https://chess.com/g/3", MINIMAL_PGN, "blitz", "Hikaru", "opp2", "C20")));

    worker.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-02", false));

    // hikaru and opp1 are looked up once in January (concurrently, so order within the month is
    // not guaranteed) and reused in February; opp2 is new
    assertThat(stubChessClient.getPlayerFetchCalls())
        .containsExactlyInAnyOrder("hikaru", "opp1", "opp2");
    assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
  }

  @Test
  public void process_skipCacheBypassesCachedPeriodAndRefetches() {
    periodStore.setCachedPeriod(
        PLAYER,
        PLATFORM,
        "2024-01",
        new IndexedPeriodStore.IndexedPeriod(
            PLAYER, PLATFORM, "2024-01", Instant.EPOCH, true, 7, false));
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(playedGame("https://chess.com/g/refetch", MINIMAL_PGN, "blitz")));
    RecordingGameFeatureStore store = new RecordingGameFeatureStore();
    IndexWorker w =
        new IndexWorker(
            stubChessClient,
            featureExtractor,
            requestStore,
            store,
            periodStore,
            extractionExecutor);

    w.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false, true));

    // The cached period is ignored: the month is fetched and the row rewritten
    assertThat(stubChessClient.getFetchCalls()).containsExactly(java.time.YearMonth.of(2024, 1));
    assertThat(store.getInsertCount()).isEqualTo(1);
    assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
  }

  @Test
  public void process_titleLookupErrorStoresPeriodIncompleteForRefetch() {
    stubChessClient.setThrowOnFetchPlayer(new RuntimeException("chess.com API returned HTTP 429"));
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(playedGame("https://chess.com/g/degraded", MINIMAL_PGN, "blitz")));

    worker.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false));

    // Games are still indexed, but the period must not be cached as complete — otherwise the
    // null titles would be frozen until someone manually clears indexed_periods.
    assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
    assertThat(periodStore.getUpserts())
        .containsExactly(new StubPeriodStore.Upsert("2024-01", false, 1));
  }

  @Test
  public void process_cleanTitleLookupsStorePeriodComplete() {
    stubChessClient.setTitle("white", "GM");
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(playedGame("https://chess.com/g/clean", MINIMAL_PGN, "blitz")));

    worker.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false));

    // "black" resolves to not-found (untitled) — a successful lookup, so the period stays
    // complete; only API errors degrade it.
    assertThat(periodStore.getUpserts())
        .containsExactly(new StubPeriodStore.Upsert("2024-01", true, 1));
  }

  @Test
  public void process_titleLookupFailureDoesNotFailIndexing() {
    stubChessClient.setThrowOnFetchPlayer(new RuntimeException("chess.com API returned HTTP 429"));
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(playedGame("https://chess.com/g/1", MINIMAL_PGN, "blitz")));
    RecordingGameFeatureStore store = new RecordingGameFeatureStore();
    IndexWorker w =
        new IndexWorker(
            stubChessClient,
            featureExtractor,
            requestStore,
            store,
            periodStore,
            extractionExecutor);

    w.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false));

    assertThat(store.getInsertCount()).isEqualTo(1);
    assertThat(store.getInsertedFeatures().get(0).whiteTitle()).isNull();
    assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
  }

  private static PlayedGame playedGame(
      String gameUrl,
      String pgn,
      String timeClass,
      String whiteUser,
      String blackUser,
      String eco) {
    return new PlayedGame(
        gameUrl,
        pgn,
        Instant.EPOCH,
        true,
        new Accuracies(90.0, 85.0),
        "",
        "uuid-" + gameUrl.hashCode(),
        "",
        "",
        timeClass,
        "chess",
        new PlayerResult(1500, "win", "https://chess.com/w", whiteUser, "uuid-w"),
        new PlayerResult(1500, "loss", "https://chess.com/b", blackUser, "uuid-b"),
        eco);
  }

  private static PlayedGame playedGame(String gameUrl, String pgn, String timeClass) {
    return new PlayedGame(
        gameUrl,
        pgn,
        Instant.EPOCH,
        true,
        new Accuracies(90.0, 85.0),
        "",
        "uuid-" + gameUrl.hashCode(),
        "",
        "",
        timeClass,
        "chess",
        new PlayerResult(1500, "win", "https://chess.com/w", "White", "uuid-w"),
        new PlayerResult(1500, "loss", "https://chess.com/b", "Black", "uuid-b"),
        "C20");
  }

  private static final String MINIMAL_PGN =
      """
      [Event "Live Chess"]
      [Site "Chess.com"]
      [White "White"]
      [Black "Black"]
      [Result "1-0"]
      [ECO "C20"]

      1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 1-0
      """;

  /** Scholar's mate: ends with Qxf7# so CheckDetector fires. */
  private static final String SCHOLARS_MATE_PGN =
      """
      [Event "Live Chess"]
      [Site "Chess.com"]
      [White "White"]
      [Black "Black"]
      [Result "1-0"]
      [ECO "C20"]

      1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7# 1-0
      """;

  /**
   * Two checks in one game: 3...Qe5+ down the open e-file, then 6.Nxc7+ forking from c7. A
   * one-check fixture cannot tell "count the occurrences" from "count one per game", which is a
   * real way for a motif chart to under-report by exactly the amount that matters.
   */
  private static final String TWO_CHECKS_PGN =
      """
      [Event "Live Chess"]
      [Site "Chess.com"]
      [White "White"]
      [Black "Black"]
      [Result "*"]
      [ECO "B01"]

      1. e4 d5 2. exd5 Qxd5 3. Nc3 Qe5+ 4. Be2 Qg5 5. Nb5 Na6 6. Nxc7+ Kd8 *
      """;

  private static final class StubChessClient extends ChessClient {
    private final List<java.time.YearMonth> fetchCalls = new ArrayList<>();
    private final Map<java.time.YearMonth, List<PlayedGame>> responseByMonth = new HashMap<>();
    private final java.util.Set<java.time.YearMonth> notFoundMonths = new java.util.HashSet<>();
    private final Map<String, String> titlesByPlayer = new HashMap<>();
    // Title lookups run on the extraction pool, so record them thread-safely
    private final List<String> playerFetchCalls = Collections.synchronizedList(new ArrayList<>());
    private RuntimeException throwOnFetch = null;
    private RuntimeException throwOnFetchPlayer = null;

    StubChessClient() {
      super(null, new ObjectMapper());
    }

    void setResponse(java.time.YearMonth month, List<PlayedGame> games) {
      responseByMonth.put(month, new ArrayList<>(games));
      notFoundMonths.remove(month);
    }

    /** Optional.empty(), the shape ChessClient gives a chess.com 404. */
    void setNotFound(java.time.YearMonth month) {
      notFoundMonths.add(month);
      responseByMonth.remove(month);
    }

    void setThrowOnFetch(RuntimeException ex) {
      this.throwOnFetch = ex;
    }

    private boolean interruptDuringFetch;

    /** Interrupts the run from inside the call, the way the MAX_RUN ceiling does. */
    void setInterruptDuringFetch() {
      this.interruptDuringFetch = true;
    }

    void setTitle(String player, String title) {
      titlesByPlayer.put(player.toLowerCase(), title);
    }

    void setThrowOnFetchPlayer(RuntimeException ex) {
      this.throwOnFetchPlayer = ex;
    }

    @Override
    public Optional<GamesResponse> fetchGames(String player, java.time.YearMonth yearMonth) {
      fetchCalls.add(yearMonth);
      if (throwOnFetch != null) {
        throw throwOnFetch;
      }
      if (interruptDuringFetch) {
        Thread.currentThread().interrupt();
      }
      if (notFoundMonths.contains(yearMonth)) {
        return Optional.empty();
      }
      List<PlayedGame> games = responseByMonth.get(yearMonth);
      // Default: empty archive (HTTP 200), not a 404. ChessClient only returns empty on 404.
      if (games == null) {
        return Optional.of(new GamesResponse(List.of()));
      }
      return Optional.of(new GamesResponse(games));
    }

    @Override
    public Optional<Player> fetchPlayer(String player) {
      playerFetchCalls.add(player.toLowerCase());
      if (throwOnFetchPlayer != null) {
        throw throwOnFetchPlayer;
      }
      String title = titlesByPlayer.get(player.toLowerCase());
      if (title == null) {
        return Optional.empty();
      }
      return Optional.of(
          new Player(
              0,
              null,
              null,
              null,
              player,
              0,
              null,
              Instant.EPOCH,
              Instant.EPOCH,
              null,
              false,
              false,
              null,
              List.of(),
              title,
              null,
              null));
    }

    List<java.time.YearMonth> getFetchCalls() {
      return new ArrayList<>(fetchCalls);
    }

    List<String> getPlayerFetchCalls() {
      return new ArrayList<>(playerFetchCalls);
    }
  }

  private static final class RecordingRequestStore implements IndexingRequestStore {
    // When set, the terminal write is refused the way a lost lease refuses it. Progress writes
    // still succeed, so the run gets all the way to the end before discovering the range moved.
    private boolean refuseTerminalWrite;

    void refuseTerminalWrite() {
      this.refuseTerminalWrite = true;
    }

    private String lastStatus;
    private String lastErrorMessage;
    private int lastGamesIndexed;

    String getLastStatus() {
      return lastStatus;
    }

    String getLastErrorMessage() {
      return lastErrorMessage;
    }

    int getLastGamesIndexed() {
      return lastGamesIndexed;
    }

    @Override
    public Claim createOrAdopt(
        String player,
        String platform,
        String startMonth,
        String endMonth,
        boolean excludeBullet,
        boolean skipCache,
        java.time.Duration staleAfter,
        Instant now) {
      return new Claim(
          new IndexingRequestStore.IndexingRequest(
              UUID.randomUUID(),
              player,
              platform,
              startMonth,
              endMonth,
              "PENDING",
              now,
              now,
              null,
              0,
              excludeBullet,
              false,
              0),
          true);
    }

    /** Not exercised by IndexWorker tests: nothing here dispatches from the table. */
    @Override
    public Optional<IndexingRequestStore.IndexingRequest> claimNext(
        String ownerId, java.time.Duration lease, Instant now) {
      return Optional.empty();
    }

    @Override
    public Optional<IndexingRequestStore.IndexingRequest> findById(UUID id) {
      return Optional.empty();
    }

    @Override
    public int reclaimStale(java.time.Duration staleAfter, Instant now) {
      return 0;
    }

    @Override
    public boolean claim(UUID id, String ownerId, java.time.Duration lease, Instant now) {
      return true;
    }

    @Override
    public boolean renewLease(UUID id, String ownerId, java.time.Duration lease, Instant now) {
      return true;
    }

    @Override
    public boolean handBack(UUID id, String ownerId, Instant now) {

      return false;
    }

    @Override
    public boolean releaseOwned(UUID id, String ownerId, Instant now) {

      return false;
    }

    @Override
    public boolean holdsLease(UUID id, String ownerId, Instant now) {
      return true;
    }

    @Override
    public boolean updateStatusOwned(
        UUID id,
        String ownerId,
        String status,
        String errorMessage,
        int gamesIndexed,
        Instant now) {
      if (refuseTerminalWrite && !"PROCESSING".equals(status)) {
        return false;
      }
      updateStatus(id, status, errorMessage, gamesIndexed);
      return true;
    }

    @Override
    public int deleteOlderThan(Instant threshold) {
      return 0;
    }

    @Override
    public void updateStatus(UUID id, String status, String errorMessage, int gamesIndexed) {
      this.lastStatus = status;
      this.lastErrorMessage = errorMessage;
      this.lastGamesIndexed = gamesIndexed;
    }

    @Override
    public List<IndexingRequestStore.IndexingRequest> listRecent(int limit) {
      return List.of();
    }

    @Override
    public Optional<IndexingRequestStore.IndexingRequest> findExistingRequest(
        String player, String platform, String startMonth, String endMonth, boolean excludeBullet) {
      return Optional.empty();
    }
  }

  private static class NoOpGameFeatureStore implements GameFeatureStore {
    @Override
    public void insertBatch(List<GameFeature> features) {}

    /**
     * Composed from the primitives rather than stubbed out, so a subclass that records only {@code
     * insertBatch} still sees what a flush wrote. The production DAO does the same three things;
     * the difference is that it does them in one transaction, which a fake cannot model and which
     * the H2-backed ConcurrentFlushTest covers instead.
     */
    @Override
    public boolean flushOwned(
        UUID requestId,
        String ownerId,
        Instant now,
        List<GameFeature> features,
        Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame) {
      insertBatch(features);
      deleteOccurrencesByGameUrls(new ArrayList<>(occurrencesByGame.keySet()));
      insertOccurrencesBatch(occurrencesByGame);
      return true;
    }

    @Override
    public int deleteOlderThan(Instant threshold) {
      return 0;
    }

    @Override
    public void insertOccurrencesBatch(
        Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame) {}

    @Override
    public List<GameFeature> query(Object compiledQuery, int limit, int offset) {
      return Collections.emptyList();
    }

    @Override
    public List<com.muchq.games.one_d4.api.dto.AggregateRow> aggregate(
        Object compiledQuery, List<String> groupColumns, boolean withOutcomeMetrics, int limit) {
      return Collections.emptyList();
    }

    @Override
    public AggregateTotals aggregateTotals(Object compiledQuery) {
      return new AggregateTotals(0, 0);
    }

    @Override
    public Map<String, Map<String, List<OccurrenceRow>>> queryOccurrences(List<String> gameUrls) {
      return Map.of();
    }

    @Override
    public void deleteOccurrencesByGameUrls(List<String> gameUrls) {}

    @Override
    public java.util.List<GameOpening> fetchOpeningsForRederive(int limit, int offset) {
      return java.util.List.of();
    }

    @Override
    public int updateOpeningFamilies(java.util.List<GameOpening> updates) {
      return 0;
    }

    @Override
    public List<GameForReanalysis> fetchForReanalysis(int limit, int offset) {
      return Collections.emptyList();
    }
  }

  private static final class RecordingGameFeatureStore extends NoOpGameFeatureStore {
    private final Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>>
        allInsertedOccurrences = new HashMap<>();
    private final List<String> insertedUrls = new ArrayList<>();
    private final List<GameFeature> insertedFeatures = new ArrayList<>();
    private int insertCount = 0;

    List<GameFeature> getInsertedFeatures() {
      return new ArrayList<>(insertedFeatures);
    }

    Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> getAllInsertedOccurrences() {
      return allInsertedOccurrences;
    }

    int getInsertCount() {
      return insertCount;
    }

    List<String> getInsertedUrls() {
      return insertedUrls;
    }

    @Override
    public void insertBatch(List<GameFeature> features) {
      insertCount += features.size();
      for (GameFeature f : features) {
        insertedUrls.add(f.gameUrl());
        insertedFeatures.add(f);
      }
    }

    @Override
    public void insertOccurrencesBatch(
        Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame) {
      allInsertedOccurrences.putAll(occurrencesByGame);
    }
  }

  private static final class StubPeriodStore implements IndexedPeriodStore {
    private final Map<String, IndexedPeriodStore.IndexedPeriod> cachedPeriods = new HashMap<>();
    private final List<Upsert> upserts = new ArrayList<>();

    record Upsert(String month, boolean isComplete, int gamesCount) {}

    void setCachedPeriod(
        String player, String platform, String month, IndexedPeriodStore.IndexedPeriod period) {
      cachedPeriods.put(key(player, platform, month, period.excludeBullet()), period);
    }

    List<Upsert> getUpserts() {
      return new ArrayList<>(upserts);
    }

    private static String key(String player, String platform, String month, boolean excludeBullet) {
      return player + "|" + platform + "|" + month + "|" + excludeBullet;
    }

    @Override
    public Optional<IndexedPeriod> findCompletePeriod(
        String player, String platform, String month, boolean excludeBullet) {
      return Optional.ofNullable(cachedPeriods.get(key(player, platform, month, excludeBullet)));
    }

    @Override
    public void upsertPeriod(
        String player,
        String platform,
        String month,
        Instant fetchedAt,
        boolean isComplete,
        int gamesCount,
        boolean excludeBullet) {
      upserts.add(new Upsert(month, isComplete, gamesCount));
    }

    @Override
    public List<IndexedPeriod> findPeriodsForPlayers(Collection<String> players) {
      return List.of();
    }

    @Override
    public int deleteOlderThan(Instant threshold) {
      return 0;
    }
  }

  // ---------------------------------------------------------------------------
  // What the indexer reports about its own work (#1212).
  //
  // The dashboard showed request counts and nothing about indexing, because a Java service had no
  // way to record anything else. These assert the instruments the one_d4 registry entry queries,
  // and they assert values rather than presence: a counter stuck at zero is exactly what a broken
  // emit site produces, and it renders as a flat line rather than as an error.
  // ---------------------------------------------------------------------------

  private CustomMetrics metrics;

  private IndexWorker meteredWorker() {
    metrics = new CustomMetrics();
    // Its own extractor: the shared one in setUp has no CheckDetector, so the motif assertions
    // below would pass against a worker that never counted anything.
    // Two detectors, so the motif label is a dimension rather than a constant: with only one,
    // replacing motif.name() with the literal "check" would pass.
    FeatureExtractor withCheck =
        new FeatureExtractor(
            new PgnParser(),
            new GameReplayer(),
            List.of(new CheckDetector(), new AttackDetector()));
    return new IndexWorker(
        stubChessClient,
        withCheck,
        requestStore,
        new NoOpGameFeatureStore(),
        periodStore,
        extractionExecutor,
        java.time.Clock.systemUTC(),
        IndexWorker.DEFAULT_HEARTBEAT_INTERVAL,
        metrics);
  }

  /**
   * The zero baseline (#1313). Constructing the worker must put every bounded series on the wire at
   * 0, before any run — otherwise the series is born carrying its first run's numbers and
   * increase() shows nothing for that run, forever. That is what hid a thousand-game overnight run
   * behind a panel of zeros: the counter read 1092 and every dashboard read 0.
   */
  @Test
  public void constructionDeclaresEveryBoundedSeriesAtZero() {
    meteredWorker();

    assertThat(counter(IndexWorker.GAMES_INDEXED, Map.of())).isZero();
    for (String outcome : IndexWorker.RUN_OUTCOMES) {
      assertThat(counter(IndexWorker.RUNS, Map.of("outcome", outcome)))
          .as("run outcome %s", outcome)
          .isZero();
    }
    for (String result : IndexWorker.MONTH_RESULTS) {
      assertThat(counter(IndexWorker.MONTHS, Map.of("result", result)))
          .as("month result %s", result)
          .isZero();
    }
    for (String result : IndexWorker.ARCHIVE_FETCH_RESULTS) {
      assertThat(counter(IndexWorker.ARCHIVE_FETCHES, Map.of("result", result)))
          .as("archive fetch result %s", result)
          .isZero();
    }
    // Presence, not just a zero read: counter() sums an empty stream to 0, so the assertions
    // above pass just as happily against a worker that declared nothing at all.
    assertThat(metrics.counterSnapshot()).isNotEmpty();

    // The distributions too. defineDistribution declares bounds, not a series, so these need
    // their own declaration or the first run's duration is the value the series is born with —
    // and avg_run_seconds_1h divides a one-sample rate by a one-sample rate.
    //
    // Every outcome, not just "completed": production declares RUN_DURATION once per
    // RUN_OUTCOMES entry, and a run that fails or loses its lease is exactly the run someone
    // goes looking for. Pinning one spelling would let the other three drop out unnoticed.
    assertThat(metrics.distributionSnapshot())
        .extracting(
            CustomMetrics.DistributionSnapshot::name, CustomMetrics.DistributionSnapshot::labels)
        .contains(
            org.assertj.core.groups.Tuple.tuple(
                IndexWorker.GAMES_PER_MONTH,
                Map.of(IndexWorker.INDEXER_LABEL, IndexWorker.INDEXER_VALUE)));
    assertThat(distributionCount(IndexWorker.GAMES_PER_MONTH)).isZero();
    for (String outcome : IndexWorker.RUN_OUTCOMES) {
      Map<String, String> labels =
          Map.of(IndexWorker.INDEXER_LABEL, IndexWorker.INDEXER_VALUE, "outcome", outcome);
      assertThat(metrics.distributionSnapshot())
          .as("run duration series for outcome %s", outcome)
          .extracting(
              CustomMetrics.DistributionSnapshot::name, CustomMetrics.DistributionSnapshot::labels)
          .contains(
              org.assertj.core.groups.Tuple.tuple(
                  IndexWorker.RUN_DURATION,
                  Map.of(
                      IndexWorker.INDEXER_LABEL, IndexWorker.INDEXER_VALUE, "outcome", outcome)));
      assertThat(distributionCount(IndexWorker.RUN_DURATION, labels))
          .as("run duration count for outcome %s", outcome)
          .isZero();
    }
  }

  /**
   * The declarations and the emit sites have to agree, or a declared-but-never-emitted series is a
   * permanently flat line and an emitted-but-never-declared one loses its first event. A run emits
   * only series this list already knows about.
   */
  @Test
  public void aRunEmitsNoSeriesThatConstructionDidNotDeclare() {
    IndexWorker metered = meteredWorker();
    java.util.Set<String> declared =
        metrics.counterSnapshot().stream()
            .map(sn -> sn.name() + sn.labels())
            .collect(java.util.stream.Collectors.toSet());

    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(playedGame("https://chess.com/g/baseline", SCHOLARS_MATE_PGN, "blitz")));
    metered.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", false));

    assertThat(
            metrics.counterSnapshot().stream()
                .map(sn -> sn.name() + sn.labels())
                .filter(key -> !declared.contains(key))
                // motif_occurrences is labelled per motif and deliberately left undeclared.
                .filter(key -> !key.startsWith(IndexWorker.MOTIF_OCCURRENCES))
                .toList())
        .as("every counter a run touches must have had a zero baseline waiting for it")
        .isEmpty();
  }

  @Test
  public void metrics_everySeriesCarriesTheIndexerLabel() {
    // Attribution, not arithmetic: the share tile's denominator selects on
    // service_name and computes the same number whatever this worker stamps.
    // What the label buys is asking which indexer wrote a series directly,
    // and an answer that survives the two being co-deployed. Half a pair is
    // worse than neither — cpp labelled and java not reads as every series
    // being cpp's.
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(playedGame("https://chess.com/game/lbl", SCHOLARS_MATE_PGN, "blitz")));
    IndexWorker w = meteredWorker();

    w.process(oneMonth());

    assertThat(metrics.counterSnapshot()).isNotEmpty();
    assertThat(metrics.counterSnapshot())
        .allSatisfy(s -> assertThat(s.labels()).containsEntry("indexer", "java"));
    // The distributions too. index_games_per_month is only ever asserted
    // through the name-only helper, so dropping its labels leaves a declared
    // series pinned at zero beside a second unlabelled one carrying every
    // real observation — and every other test still green.
    assertThat(metrics.distributionSnapshot()).isNotEmpty();
    assertThat(metrics.distributionSnapshot())
        .allSatisfy(s -> assertThat(s.labels()).containsEntry("indexer", "java"));
  }

  /**
   * Still an exact match on the whole label set — the indexer label is merged in rather than
   * ignored, so a series that carries the wrong value still fails. {@link
   * #metrics_everySeriesCarriesTheIndexerLabel} is what proves the label is there at all, which is
   * the part this merge would otherwise hide.
   */
  private long counter(String name, Map<String, String> labels) {
    Map<String, String> expected = new java.util.HashMap<>(labels);
    expected.put(IndexWorker.INDEXER_LABEL, IndexWorker.INDEXER_VALUE);
    return metrics.counterSnapshot().stream()
        .filter(s -> s.name().equals(name) && s.labels().equals(expected))
        .mapToLong(CustomMetrics.CounterSnapshot::value)
        .sum();
  }

  private long distributionCount(String name) {
    return metrics.distributionSnapshot().stream()
        .filter(s -> s.name().equals(name))
        .mapToLong(CustomMetrics.DistributionSnapshot::count)
        .sum();
  }

  /** Merges the indexer label like {@link #counter}, and for the same reason. */
  private long distributionCount(String name, Map<String, String> labels) {
    Map<String, String> expected = new java.util.HashMap<>(labels);
    expected.put(IndexWorker.INDEXER_LABEL, IndexWorker.INDEXER_VALUE);
    return metrics.distributionSnapshot().stream()
        .filter(s -> s.name().equals(name) && s.labels().equals(expected))
        .mapToLong(CustomMetrics.DistributionSnapshot::count)
        .sum();
  }

  private double distributionSum(String name) {
    return metrics.distributionSnapshot().stream()
        .filter(s -> s.name().equals(name))
        .mapToDouble(CustomMetrics.DistributionSnapshot::sum)
        .sum();
  }

  private long runDurationOverflowCount() {
    return metrics.distributionSnapshot().stream()
        .filter(d -> d.name().equals(IndexWorker.RUN_DURATION))
        .mapToLong(d -> d.bucketCounts()[d.bucketCounts().length - 1])
        .sum();
  }

  private static IndexMessage oneMonth() {
    return new IndexMessage(UUID.randomUUID(), PLAYER, PLATFORM, "2024-01", "2024-01", false);
  }

  @Test
  public void metrics_completedRunReportsOutcomeAndGamesIndexed() {
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(
            playedGame("https://chess.com/game/m1", SCHOLARS_MATE_PGN, "blitz"),
            playedGame("https://chess.com/game/m2", SCHOLARS_MATE_PGN, "blitz"),
            playedGame("https://chess.com/game/m3", SCHOLARS_MATE_PGN, "blitz")));
    IndexWorker w = meteredWorker();

    w.process(oneMonth());

    assertThat(counter(IndexWorker.RUNS, Map.of("outcome", "completed"))).isEqualTo(1);
    assertThat(counter(IndexWorker.GAMES_INDEXED, Map.of()))
        .as("the headline number the dashboard exists to show")
        .isEqualTo(3);
    assertThat(counter(IndexWorker.MONTHS, Map.of("result", "indexed"))).isEqualTo(1);
    assertThat(counter(IndexWorker.ARCHIVE_FETCHES, Map.of("result", "ok"))).isEqualTo(1);
    assertThat(distributionCount(IndexWorker.RUN_DURATION)).isEqualTo(1);
    // Under the outcome label, not bare. prom_proxy's avg_run_seconds_1h selects
    // outcome="completed" so a ceiling-length interrupted run cannot drag the average; an
    // unlabelled histogram makes that selector match nothing and the tile reads empty.
    assertThat(distributionCount(IndexWorker.RUN_DURATION, Map.of("outcome", "completed")))
        .as("the duration histogram is labelled the same way the run counter is")
        .isEqualTo(1);
    // The bounds, not just the count. CustomMetricsTest pins that declared bounds are honoured;
    // nothing pinned that this worker declares any, and without the call every run lands in the
    // overflow bucket of a histogram whose top bound is 10ms — fifteen dead series, and a p95 that
    // answers a flat 10000 rather than failing loudly, with _sum and _count still perfectly
    // correct. A histogram that reads like a fast run is why this is pinned and not argued.
    assertThat(metrics.boundsFor(IndexWorker.RUN_DURATION))
        .as("run duration must not be bucketed on the HTTP latency bounds")
        .isEqualTo(IndexWorker.RUN_DURATION_BOUNDS);
    assertThat(runDurationOverflowCount())
        .as("a run of ordinary length belongs in a real bucket, not the overflow")
        .isZero();
    assertThat(distributionSum(IndexWorker.GAMES_PER_MONTH))
        .as("per-month shape, not just the run total")
        .isEqualTo(3.0);
    assertThat(metrics.boundsFor(IndexWorker.GAMES_PER_MONTH))
        .as("counts get count-shaped buckets, not the HTTP latency set they happen to fit")
        .isEqualTo(IndexWorker.GAMES_PER_MONTH_BOUNDS);
  }

  /**
   * A month with an empty archive is indexed — the answer is "none". Counting it as a failure would
   * make a quiet player look like an outage.
   */
  @Test
  public void metrics_monthWithNoArchiveCountsAsIndexedNotFailed() {
    stubChessClient.setResponse(java.time.YearMonth.of(2024, 1), List.of());
    IndexWorker w = meteredWorker();

    w.process(oneMonth());

    assertThat(counter(IndexWorker.ARCHIVE_FETCHES, Map.of("result", "no_archive"))).isEqualTo(1);
    assertThat(counter(IndexWorker.MONTHS, Map.of("result", "empty"))).isEqualTo(1);
    assertThat(counter(IndexWorker.GAMES_INDEXED, Map.of())).isZero();
    assertThat(counter(IndexWorker.RUNS, Map.of("outcome", "completed"))).isEqualTo(1);
    // A decade-long backfill of a three-year player is mostly empty archives. Feeding those zeros
    // into the per-month distribution makes the average archive read a third its real size, and
    // they are already counted as empty months.
    assertThat(distributionCount(IndexWorker.GAMES_PER_MONTH))
        .as("an empty month is counted, not averaged in")
        .isZero();
  }

  @Test
  public void metrics_motifOccurrencesAreCountedByMotif() {
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(playedGame("https://chess.com/game/mm1", SCHOLARS_MATE_PGN, "blitz")));
    IndexWorker w = meteredWorker();

    w.process(oneMonth());

    assertThat(counter(IndexWorker.MOTIF_OCCURRENCES, Map.of("motif", "check")))
        .as("scholar's mate ends in Qxf7#, so CheckDetector must have found one")
        .isPositive();

    // The label has to be a dimension, not a constant. With one motif in play, hardcoding
    // "check" at the emit site is indistinguishable from reading motif.name() — so assert that a
    // second detector's findings arrive under their own label.
    List<String> motifLabels =
        metrics.counterSnapshot().stream()
            .filter(c -> c.name().equals(IndexWorker.MOTIF_OCCURRENCES))
            .map(c -> c.labels().get("motif"))
            .toList();
    assertThat(motifLabels)
        .as("two detectors are registered, so more than one motif name must appear")
        .hasSizeGreaterThan(1);
    assertThat(motifLabels).doesNotHaveDuplicates();
  }

  /**
   * Every occurrence counts, not one per game. With a single-check fixture "add occurrences.size()"
   * and "add 1" are the same statement, and the difference is a motif chart that silently reports
   * games-containing-a-motif while claiming to report occurrences.
   */
  @Test
  public void metrics_countsEveryOccurrenceNotOnePerGame() {
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(playedGame("https://chess.com/game/two", TWO_CHECKS_PGN, "blitz")));
    IndexWorker w = meteredWorker();

    w.process(oneMonth());

    assertThat(counter(IndexWorker.MOTIF_OCCURRENCES, Map.of("motif", "check")))
        .as("one game, two checks")
        .isGreaterThanOrEqualTo(2);
  }

  /** Two runs over the same motif accumulate rather than overwrite. */
  @Test
  public void metrics_motifCountsAccumulateAcrossGames() {
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(playedGame("https://chess.com/game/ma1", SCHOLARS_MATE_PGN, "blitz")));
    IndexWorker w = meteredWorker();
    w.process(oneMonth());
    long afterOne = counter(IndexWorker.MOTIF_OCCURRENCES, Map.of("motif", "check"));
    assertThat(afterOne).isPositive();

    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(
            playedGame("https://chess.com/game/ma2", SCHOLARS_MATE_PGN, "blitz"),
            playedGame("https://chess.com/game/ma3", SCHOLARS_MATE_PGN, "blitz")));
    w.process(oneMonth());

    assertThat(counter(IndexWorker.MOTIF_OCCURRENCES, Map.of("motif", "check")))
        .isEqualTo(afterOne * 3);
  }

  /**
   * Title enrichment must never fail indexing, so a profile lookup that errors leaves the month
   * indexed but incomplete. That is worth seeing on its own: a chart where every month is degraded
   * is a chess.com problem, and one where none are is the healthy case — reporting both as
   * "indexed" hides the difference entirely.
   */
  @Test
  public void metrics_monthWithFailedTitleLookupIsReportedDegraded() {
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(playedGame("https://chess.com/game/deg", SCHOLARS_MATE_PGN, "blitz")));
    stubChessClient.setThrowOnFetchPlayer(new RuntimeException("429 from chess.com"));
    IndexWorker w = meteredWorker();

    w.process(oneMonth());

    assertThat(counter(IndexWorker.MONTHS, Map.of("result", "degraded"))).isEqualTo(1);
    assertThat(counter(IndexWorker.MONTHS, Map.of("result", "indexed"))).isZero();
    assertThat(counter(IndexWorker.RUNS, Map.of("outcome", "completed")))
        .as("degraded titles must not turn into a failed run")
        .isEqualTo(1);
  }

  @Test
  public void metrics_runThatThrowsIsReportedFailedNotCompleted() {
    stubChessClient.setThrowOnFetch(new RuntimeException("chess.com is down"));
    IndexWorker w = meteredWorker();

    w.process(oneMonth());

    assertThat(counter(IndexWorker.RUNS, Map.of("outcome", "failed"))).isEqualTo(1);
    assertThat(counter(IndexWorker.RUNS, Map.of("outcome", "completed"))).isZero();
    assertThat(distributionCount(IndexWorker.RUN_DURATION))
        .as("a failed run still took time, and that time is the interesting part")
        .isEqualTo(1);
    assertThat(distributionCount(IndexWorker.RUN_DURATION, Map.of("outcome", "failed")))
        .as("and it is recorded under failed, so it stays out of the completed-run average")
        .isEqualTo(1);
    assertThat(distributionCount(IndexWorker.RUN_DURATION, Map.of("outcome", "completed")))
        .isZero();
    // ChessClient maps only a 404 to empty and throws on everything else, so without an explicit
    // count here the rate-limits and 5xx — the archive failures worth alerting on — are the one
    // outcome the counter cannot show.
    assertThat(counter(IndexWorker.ARCHIVE_FETCHES, Map.of("result", "error"))).isEqualTo(1);
    assertThat(counter(IndexWorker.ARCHIVE_FETCHES, Map.of("result", "ok"))).isZero();
  }

  /**
   * A month served from the period cache is neither indexed nor empty, and without its own result
   * it is counted under none of them — so sum(index_months_total) is not months processed, and
   * cache effectiveness, the thing that decides how much chess.com traffic this service makes, is
   * invisible.
   */
  @Test
  public void metrics_cachedMonthIsCountedAsCached() {
    periodStore.setCachedPeriod(
        PLAYER,
        PLATFORM,
        "2024-01",
        new IndexedPeriodStore.IndexedPeriod(
            PLAYER, PLATFORM, "2024-01", Instant.EPOCH, true, 9, false));
    IndexWorker w = meteredWorker();

    w.process(oneMonth());

    assertThat(counter(IndexWorker.MONTHS, Map.of("result", "cached"))).isEqualTo(1);
    assertThat(counter(IndexWorker.MONTHS, Map.of("result", "indexed"))).isZero();
    assertThat(counter(IndexWorker.ARCHIVE_FETCHES, Map.of("result", "ok")))
        .as("a cached month must not look like a chess.com fetch")
        .isZero();
  }

  /**
   * The privacy sweep. Every distinct label set is a stored series, so one label carrying a player
   * name turns the metrics backend into a directory of who uses the service, growing without bound.
   * Asserted over every emitted series rather than at each call site, so an emit site added later
   * cannot quietly opt out.
   */
  @Test
  public void metrics_noLabelCarriesAPlayerOrGameIdentifier() {
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(playedGame("https://chess.com/game/priv", SCHOLARS_MATE_PGN, "blitz")));
    IndexWorker w = meteredWorker();
    w.process(oneMonth());

    List<String> values = new ArrayList<>();
    metrics.counterSnapshot().forEach(s -> values.addAll(s.labels().values()));
    metrics.distributionSnapshot().forEach(s -> values.addAll(s.labels().values()));

    assertThat(values).isNotEmpty();
    assertThat(values).doesNotContain(PLAYER);
    assertThat(values).noneSatisfy(v -> assertThat(v).contains("chess.com"));
    assertThat(values)
        .as("labels are bounded vocabularies — outcomes, results, motif names")
        .allSatisfy(v -> assertThat(v).matches("[a-z_]+"));
  }

  /**
   * The Java half of a cross-language contract. prom_proxy's one_d4 entry queries these names with
   * {@code _total} appended by the collector's Prometheus exporter, and a rename here is invisible
   * on that side: the query stays valid PromQL, matches no series, and the dashboard shows an empty
   * chart that looks exactly like an idle indexer. Pinned as literals rather than read from the
   * constants, so renaming a constant fails here instead of silently agreeing with itself. The Go
   * half is TestOneD4QueriesNameRealInstrumentsAndScopeThem.
   */
  @Test
  public void metrics_exportedInstrumentNames() {
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(playedGame("https://chess.com/game/names", SCHOLARS_MATE_PGN, "blitz")));
    IndexWorker w = meteredWorker();
    w.process(oneMonth());

    assertThat(metrics.counterSnapshot())
        .extracting(CustomMetrics.CounterSnapshot::name)
        .containsOnly(
            "index_runs",
            "games_indexed",
            "index_months",
            "chess_com_archive_fetches",
            "motif_occurrences");
    assertThat(metrics.distributionSnapshot())
        .extracting(CustomMetrics.DistributionSnapshot::name)
        .containsOnly("index_run_duration_micros", "index_games_per_month");
  }

  /**
   * Per-month, not per-run. Every other metrics test here indexes a single month, and across one
   * month `monthCount` and `totalIndexed` are the same number — so the distribution could be
   * recording the run total, or recording once outside the loop, and nothing would notice. Two
   * months with different counts is what separates them.
   */
  @Test
  public void metrics_gamesPerMonthIsRecordedPerMonth() {
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(playedGame("https://chess.com/game/jan1", SCHOLARS_MATE_PGN, "blitz")));
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 2),
        List.of(
            playedGame("https://chess.com/game/feb1", SCHOLARS_MATE_PGN, "blitz"),
            playedGame("https://chess.com/game/feb2", SCHOLARS_MATE_PGN, "blitz"),
            playedGame("https://chess.com/game/feb3", SCHOLARS_MATE_PGN, "blitz")));
    IndexWorker w = meteredWorker();

    w.process(new IndexMessage(UUID.randomUUID(), PLAYER, PLATFORM, "2024-01", "2024-02", false));

    assertThat(distributionCount(IndexWorker.GAMES_PER_MONTH))
        .as("one observation per month, not one per run")
        .isEqualTo(2);
    assertThat(distributionSum(IndexWorker.GAMES_PER_MONTH)).isEqualTo(4.0);
    assertThat(counter(IndexWorker.GAMES_INDEXED, Map.of())).isEqualTo(4);
    assertThat(counter(IndexWorker.MONTHS, Map.of("result", "indexed"))).isEqualTo(2);
    assertThat(counter(IndexWorker.RUNS, Map.of("outcome", "completed"))).isEqualTo(1);
  }

  /**
   * The wedge alarm. prom_proxy ships runs_interrupted_total as the #1282 signal that a worker was
   * stuck long enough to be given up on, and it had no test on either side — collapsing interrupted
   * into failed would have gone unnoticed while an operator was told to trust the number.
   */
  @Test
  public void metrics_interruptedRunIsReportedInterruptedNotFailed() {
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(playedGame("https://chess.com/game/int", SCHOLARS_MATE_PGN, "blitz")));
    stubChessClient.setInterruptDuringFetch();
    IndexWorker w = meteredWorker();

    w.process(oneMonth());
    // The run consumes its own interrupt on the way out; clear anything that escaped so the next
    // test in this class does not inherit it.
    Thread.interrupted();

    assertThat(counter(IndexWorker.RUNS, Map.of("outcome", "interrupted"))).isEqualTo(1);
    assertThat(counter(IndexWorker.RUNS, Map.of("outcome", "failed"))).isZero();
    assertThat(counter(IndexWorker.RUNS, Map.of("outcome", "completed"))).isZero();
  }

  /** A range that changes hands is not a failure, and the two must not share a bucket. */
  @Test
  public void metrics_runThatLosesItsLeaseIsReportedLeaseLostNotFailed() {
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1),
        List.of(playedGame("https://chess.com/game/ll", SCHOLARS_MATE_PGN, "blitz")));
    requestStore.refuseTerminalWrite();
    IndexWorker w = meteredWorker();

    w.process(oneMonth());

    assertThat(counter(IndexWorker.RUNS, Map.of("outcome", "lease_lost"))).isEqualTo(1);
    assertThat(counter(IndexWorker.RUNS, Map.of("outcome", "failed"))).isZero();
    assertThat(counter(IndexWorker.RUNS, Map.of("outcome", "completed"))).isZero();
  }
}
