package com.muchq.games.one_d4.worker;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.games.chess_com_client.Accuracies;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.GamesResponse;
import com.muchq.games.chess_com_client.PlayedGame;
import com.muchq.games.chess_com_client.PlayerResult;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.db.GameFeatureStore;
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
import java.time.Instant;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Optional;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class IndexWorkerTest {

  private static final UUID REQUEST_ID = UUID.randomUUID();
  private static final String PLAYER = "testplayer";
  private static final String PLATFORM = "CHESS_COM";

  private StubChessClient stubChessClient;
  private RecordingRequestStore requestStore;
  private StubPeriodStore periodStore;
  private IndexWorker worker;
  private FeatureExtractor featureExtractor;

  @Before
  public void setUp() {
    stubChessClient = new StubChessClient();
    requestStore = new RecordingRequestStore();
    periodStore = new StubPeriodStore();
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
            periodStore);
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
            stubChessClient, featureExtractor, requestStore, recordingStore, periodStore);

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

  @Test
  public void process_bulletGamesNotSkippedWhenExcludeBulletFalse() {
    String gameUrl = "https://chess.com/game/bullet-keep";
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1), List.of(playedGame(gameUrl, MINIMAL_PGN, "bullet")));
    RecordingGameFeatureStore recordingStore = new RecordingGameFeatureStore();
    IndexWorker w =
        new IndexWorker(
            stubChessClient, featureExtractor, requestStore, recordingStore, periodStore);

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
  public void process_bulletGamesSkippedWhenExcludeBulletTrue() {
    String gameUrl = "https://chess.com/game/bullet-skip";
    stubChessClient.setResponse(
        java.time.YearMonth.of(2024, 1), List.of(playedGame(gameUrl, MINIMAL_PGN, "bullet")));
    RecordingGameFeatureStore recordingStore = new RecordingGameFeatureStore();
    IndexWorker w =
        new IndexWorker(
            stubChessClient, featureExtractor, requestStore, recordingStore, periodStore);

    w.process(new IndexMessage(REQUEST_ID, PLAYER, PLATFORM, "2024-01", "2024-01", true));

    assertThat(recordingStore.getInsertCount()).isEqualTo(0);
    assertThat(requestStore.getLastStatus()).isEqualTo("COMPLETED");
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

  private static final class StubChessClient extends ChessClient {
    private final List<java.time.YearMonth> fetchCalls = new ArrayList<>();
    private final Map<java.time.YearMonth, List<PlayedGame>> responseByMonth = new HashMap<>();
    private RuntimeException throwOnFetch = null;

    StubChessClient() {
      super(null, new ObjectMapper());
    }

    void setResponse(java.time.YearMonth month, List<PlayedGame> games) {
      responseByMonth.put(month, new ArrayList<>(games));
    }

    void setThrowOnFetch(RuntimeException ex) {
      this.throwOnFetch = ex;
    }

    @Override
    public Optional<GamesResponse> fetchGames(String player, java.time.YearMonth yearMonth) {
      fetchCalls.add(yearMonth);
      if (throwOnFetch != null) {
        throw throwOnFetch;
      }
      List<PlayedGame> games = responseByMonth.get(yearMonth);
      if (games != null) {
        return Optional.of(new GamesResponse(games));
      }
      return Optional.empty();
    }

    List<java.time.YearMonth> getFetchCalls() {
      return new ArrayList<>(fetchCalls);
    }
  }

  private static final class RecordingRequestStore implements IndexingRequestStore {
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
    public UUID create(
        String player, String platform, String startMonth, String endMonth, boolean excludeBullet) {
      return UUID.randomUUID();
    }

    @Override
    public Optional<IndexingRequestStore.IndexingRequest> findById(UUID id) {
      return Optional.empty();
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
    public Optional<GameFeature> findByGameUrl(String gameUrl) {
      return Optional.empty();
    }

    @Override
    public void insertBatch(List<GameFeature> features) {}

    @Override
    public int deleteOlderThan(Instant threshold) {
      return 0;
    }

    @Override
    public void insertOccurrencesBatch(
        Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame) {}

    @Override
    public List<GameFeature> query(
        Object compiledQuery, int limit, int offset, boolean includePgn) {
      return Collections.emptyList();
    }

    @Override
    public int count(Object compiledQuery) {
      return 0;
    }

    @Override
    public Map<String, Map<String, List<OccurrenceRow>>> queryOccurrences(List<String> gameUrls) {
      return Map.of();
    }

    @Override
    public void deleteOccurrencesByGameUrls(List<String> gameUrls) {}

    @Override
    public List<GameForReanalysis> fetchForReanalysis(int limit, int offset) {
      return Collections.emptyList();
    }
  }

  private static final class RecordingGameFeatureStore extends NoOpGameFeatureStore {
    private final Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>>
        allInsertedOccurrences = new HashMap<>();
    private int insertCount = 0;

    Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> getAllInsertedOccurrences() {
      return allInsertedOccurrences;
    }

    int getInsertCount() {
      return insertCount;
    }

    @Override
    public void insertBatch(List<GameFeature> features) {
      insertCount += features.size();
    }

    @Override
    public void insertOccurrencesBatch(
        Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesByGame) {
      allInsertedOccurrences.putAll(occurrencesByGame);
    }
  }

  private static final class StubPeriodStore implements IndexedPeriodStore {
    private final Map<String, IndexedPeriodStore.IndexedPeriod> cachedPeriods = new HashMap<>();

    void setCachedPeriod(
        String player, String platform, String month, IndexedPeriodStore.IndexedPeriod period) {
      cachedPeriods.put(key(player, platform, month, period.excludeBullet()), period);
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
        boolean excludeBullet) {}

    @Override
    public int deleteOlderThan(Instant threshold) {
      return 0;
    }
  }
}
