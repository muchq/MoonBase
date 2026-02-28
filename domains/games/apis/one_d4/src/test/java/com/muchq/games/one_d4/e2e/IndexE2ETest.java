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
import com.muchq.games.one_d4.api.dto.IndexResponse;
import com.muchq.games.one_d4.db.DataSourceFactory;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.IndexedPeriodStore;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.db.Migration;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.GameReplayer;
import com.muchq.games.one_d4.engine.PgnParser;
import com.muchq.games.one_d4.motifs.AttackDetector;
import com.muchq.games.one_d4.motifs.CheckDetector;
import com.muchq.games.one_d4.motifs.CrossPinDetector;
import com.muchq.games.one_d4.motifs.DiscoveredCheckDetector;
import com.muchq.games.one_d4.motifs.MotifDetector;
import com.muchq.games.one_d4.motifs.PinDetector;
import com.muchq.games.one_d4.motifs.SkewerDetector;
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
 * Local e2e tests: real in-memory H2, real IndexController, IndexWorker, and stores; only the
 * chess.com API client is faked.
 */
public class IndexE2ETest {

  private static final String PLAYER = "e2eplayer";
  private static final String PLATFORM = "CHESS_COM";

  private IndexController controller;
  private IndexQueue queue;
  private IndexWorker worker;
  private GameFeatureStore gameFeatureStore;
  private FakeChessClient fakeChessClient;
  private IndexingRequestStore requestStore;
  private IndexedPeriodStore periodStore;

  @Before
  public void setUp() {
    String jdbcUrl = "jdbc:h2:mem:e2e_" + System.currentTimeMillis() + ";DB_CLOSE_DELAY=-1";
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
            new CheckDetector(),
            new PinDetector(),
            new CrossPinDetector(),
            new SkewerDetector(),
            new AttackDetector(),
            new DiscoveredCheckDetector());
    FeatureExtractor featureExtractor =
        new FeatureExtractor(new PgnParser(), new GameReplayer(), detectors);
    worker =
        new IndexWorker(
            fakeChessClient, featureExtractor, requestStore, gameFeatureStore, periodStore);

    controller = new IndexController(requestStore, queue, new IndexRequestValidator());
  }

  @Test
  public void createIndex_processToCompletion_returnsCompletedWithCorrectCount() {
    YearMonth month = YearMonth.of(2024, 3);
    fakeChessClient.addGame(PLAYER, month, "https://chess.com/game/e2e-1");
    fakeChessClient.addGame(PLAYER, month, "https://chess.com/game/e2e-2");

    IndexRequest request = new IndexRequest(PLAYER, PLATFORM, "2024-03", "2024-03");
    IndexResponse created = controller.createIndex(request);

    assertThat(created.id()).isNotNull();
    assertThat(created.status()).isEqualTo("PENDING");
    assertThat(created.gamesIndexed()).isEqualTo(0);

    processQueueUntilIdle();

    IndexResponse after = controller.getIndex(created.id());
    assertThat(after.status()).isEqualTo("COMPLETED");
    assertThat(after.gamesIndexed()).isEqualTo(2);
    assertThat(after.player()).isEqualTo(PLAYER);
    assertThat(after.platform()).isEqualTo(PLATFORM);
    assertThat(after.startMonth()).isEqualTo("2024-03");
    assertThat(after.endMonth()).isEqualTo("2024-03");
    assertThat(fakeChessClient.getFetchCalls())
        .containsExactly(new FakeChessClient.FetchCall(PLAYER, month));
  }

  @Test
  public void
      createIndex_duplicateParamsWhilePending_returnsExistingRequestAndDoesNotEnqueueSecond() {
    YearMonth month = YearMonth.of(2024, 4);
    fakeChessClient.addGame(PLAYER, month, "https://chess.com/game/dup-1");

    IndexRequest request = new IndexRequest(PLAYER, PLATFORM, "2024-04", "2024-04");
    IndexResponse first = controller.createIndex(request);
    assertThat(first.status()).isEqualTo("PENDING");
    assertThat(queue.size()).isEqualTo(1);

    IndexResponse second = controller.createIndex(request);
    assertThat(second.id()).isEqualTo(first.id());
    assertThat(second.status()).isEqualTo("PENDING");
    assertThat(queue.size()).isEqualTo(1);

    processQueueUntilIdle();
    IndexResponse after = controller.getIndex(first.id());
    assertThat(after.status()).isEqualTo("COMPLETED");
    assertThat(after.gamesIndexed()).isEqualTo(1);
  }

  @Test
  public void createIndex_withCachedPeriod_skipsFetchForCachedMonth() {
    YearMonth jan = YearMonth.of(2024, 1);
    YearMonth feb = YearMonth.of(2024, 2);
    periodStore.upsertPeriod(
        PLAYER, PLATFORM, "2024-01", Instant.parse("2024-02-01T00:00:00Z"), true, 3);

    fakeChessClient.setNoGames(PLAYER, jan);
    fakeChessClient.addGame(PLAYER, feb, "https://chess.com/game/feb-1");

    IndexRequest request = new IndexRequest(PLAYER, PLATFORM, "2024-01", "2024-02");
    IndexResponse created = controller.createIndex(request);
    processQueueUntilIdle();

    IndexResponse after = controller.getIndex(created.id());
    assertThat(after.status()).isEqualTo("COMPLETED");
    assertThat(after.gamesIndexed()).isEqualTo(3 + 1);

    List<FakeChessClient.FetchCall> calls = fakeChessClient.getFetchCalls();
    assertThat(calls).containsExactly(new FakeChessClient.FetchCall(PLAYER, feb));
  }

  @Test
  public void createIndex_withMiddleMonthCached_skipsFetchForCachedMonthOnly() {
    YearMonth jan = YearMonth.of(2024, 1);
    YearMonth feb = YearMonth.of(2024, 2);
    YearMonth mar = YearMonth.of(2024, 3);
    periodStore.upsertPeriod(
        PLAYER, PLATFORM, "2024-02", Instant.parse("2024-03-01T00:00:00Z"), true, 5);

    fakeChessClient.addGame(PLAYER, jan, "https://chess.com/game/jan-1");
    fakeChessClient.addGame(PLAYER, jan, "https://chess.com/game/jan-2");
    fakeChessClient.setNoGames(PLAYER, feb);
    fakeChessClient.addGame(PLAYER, mar, "https://chess.com/game/mar-1");

    IndexRequest request = new IndexRequest(PLAYER, PLATFORM, "2024-01", "2024-03");
    IndexResponse created = controller.createIndex(request);
    processQueueUntilIdle();

    IndexResponse after = controller.getIndex(created.id());
    assertThat(after.status()).isEqualTo("COMPLETED");
    assertThat(after.gamesIndexed()).isEqualTo(2 + 5 + 1);

    List<FakeChessClient.FetchCall> calls = fakeChessClient.getFetchCalls();
    assertThat(calls)
        .containsExactly(
            new FakeChessClient.FetchCall(PLAYER, jan), new FakeChessClient.FetchCall(PLAYER, mar));
  }

  @Test
  public void listRequests_returnsAllRequestsIncludingCompleted() {
    YearMonth month = YearMonth.of(2024, 5);
    fakeChessClient.addGame(PLAYER, month, "https://chess.com/game/list-1");

    IndexRequest request = new IndexRequest(PLAYER, PLATFORM, "2024-05", "2024-05");
    IndexResponse created = controller.createIndex(request);

    List<IndexResponse> pending = controller.listRequests();
    assertThat(pending).hasSize(1);
    assertThat(pending.get(0).id()).isEqualTo(created.id());
    assertThat(pending.get(0).status()).isEqualTo("PENDING");

    processQueueUntilIdle();

    List<IndexResponse> completed = controller.listRequests();
    assertThat(completed).hasSize(1);
    assertThat(completed.get(0).status()).isEqualTo("COMPLETED");
    assertThat(completed.get(0).gamesIndexed()).isEqualTo(1);
  }

  @Test
  public void createIndex_thenQuery_returnsGamesWithOccurrences() {
    YearMonth month = YearMonth.of(2024, 6);
    String gameUrl = "https://chess.com/game/query-occ-1";
    // Scholar's mate: Qxf7# so CheckDetector fires and we get an occurrence
    fakeChessClient.setGames(PLAYER, month, List.of(playedGameWithCheckPgn(gameUrl)));

    IndexRequest request = new IndexRequest(PLAYER, PLATFORM, "2024-06", "2024-06");
    controller.createIndex(request);
    processQueueUntilIdle();

    CompiledQuery compiled = new SqlCompiler().compile(Parser.parse("motif(check)"));
    List<GameFeature> rows = gameFeatureStore.query(compiled, 10, 0);
    assertThat(rows).hasSize(1);
    assertThat(rows.get(0).gameUrl()).isEqualTo(gameUrl);

    List<String> gameUrls = rows.stream().map(GameFeature::gameUrl).toList();
    Map<String, Map<String, List<com.muchq.games.one_d4.api.dto.OccurrenceRow>>> occurrences =
        gameFeatureStore.queryOccurrences(gameUrls);
    assertThat(occurrences).containsKey(gameUrl);
    assertThat(occurrences.get(gameUrl)).containsKey("check");
    assertThat(occurrences.get(gameUrl).get("check")).isNotEmpty();
    assertThat(occurrences.get(gameUrl).get("check").get(0).moveNumber()).isPositive();
  }

  private static PlayedGame playedGameWithCheckPgn(String gameUrl) {
    String pgn =
        """
        [Event "Live Chess"]
        [Site "Chess.com"]
        [White "White"]
        [Black "Black"]
        [Result "1-0"]
        [ECO "C20"]

        1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7# 1-0
        """;
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
        "blitz",
        "chess",
        new PlayerResult(1500, "win", "https://chess.com/w", "White", "uuid-w"),
        new PlayerResult(1500, "loss", "https://chess.com/b", "Black", "uuid-b"),
        "C20");
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
