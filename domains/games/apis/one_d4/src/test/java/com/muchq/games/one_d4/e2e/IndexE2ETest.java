package com.muchq.games.one_d4.e2e;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chess_com_client.Accuracies;
import com.muchq.games.chess_com_client.PlayedGame;
import com.muchq.games.chess_com_client.PlayerResult;
import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.IndexController;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.IndexRequest;
import com.muchq.games.one_d4.api.dto.IndexResponse;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.IndexedPeriodStore;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.db.RetentionPolicy;
import com.muchq.games.one_d4.db.TestDb;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.GameReplayer;
import com.muchq.games.one_d4.engine.PgnParser;
import com.muchq.games.one_d4.motifs.AttackDetector;
import com.muchq.games.one_d4.motifs.CheckDetector;
import com.muchq.games.one_d4.motifs.CrossPinDetector;
import com.muchq.games.one_d4.motifs.MotifDetector;
import com.muchq.games.one_d4.motifs.PinDetector;
import com.muchq.games.one_d4.motifs.SkewerDetector;
import com.muchq.games.one_d4.queue.InMemoryIndexQueue;
import com.muchq.games.one_d4.queue.IndexMessage;
import com.muchq.games.one_d4.queue.IndexQueue;
import com.muchq.games.one_d4.service.DataAvailabilityResolver;
import com.muchq.games.one_d4.service.IndexRequestService;
import com.muchq.games.one_d4.worker.IndexWorker;
import com.muchq.games.one_d4.worker.RetentionWorker;
import java.time.Duration;
import java.time.Instant;
import java.time.YearMonth;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

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
  private java.util.concurrent.ExecutorService extractionExecutor;

  @org.junit.After
  public void tearDownExecutor() {
    if (extractionExecutor != null) extractionExecutor.shutdownNow();
  }

  @BeforeEach
  public void setUp() {
    TestDb testDb = TestDb.create("e2e");

    requestStore = new com.muchq.games.one_d4.db.IndexingRequestDao(testDb.jdbi());
    periodStore =
        new com.muchq.games.one_d4.db.IndexedPeriodDao(
            testDb.jdbi(), new com.muchq.games.one_d4.db.H2SqlDialect());
    gameFeatureStore =
        new com.muchq.games.one_d4.db.GameFeatureDao(
            testDb.jdbi(), new com.muchq.games.one_d4.db.H2SqlDialect());

    queue = new InMemoryIndexQueue();
    fakeChessClient = new FakeChessClient();

    List<MotifDetector> detectors =
        List.of(
            new CheckDetector(),
            new PinDetector(),
            new CrossPinDetector(),
            new SkewerDetector(),
            new AttackDetector());
    FeatureExtractor featureExtractor =
        new FeatureExtractor(new PgnParser(), new GameReplayer(), detectors);
    extractionExecutor = java.util.concurrent.Executors.newSingleThreadExecutor();
    worker =
        new IndexWorker(
            fakeChessClient,
            featureExtractor,
            requestStore,
            gameFeatureStore,
            periodStore,
            extractionExecutor);

    controller =
        new IndexController(
            new IndexRequestService(
                requestStore, queue, worker::process, new DataAvailabilityResolver(periodStore)),
            requestStore,
            new DataAvailabilityResolver(periodStore));
  }

  @Test
  public void createIndex_processToCompletion_returnsCompletedWithCorrectCount() {
    YearMonth month = YearMonth.of(2024, 3);
    fakeChessClient.addGame(PLAYER, month, "https://chess.com/game/e2e-1");
    fakeChessClient.addGame(PLAYER, month, "https://chess.com/game/e2e-2");

    IndexRequest request = new IndexRequest(PLAYER, PLATFORM, "2024-03", "2024-03", null);
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

  /**
   * The request row survives retention; its games do not. This drives the real sweep against real
   * H2 and pins that the reported status follows the data rather than the stale request row.
   */
  @Test
  public void completedRequest_reportsDataAvailableUntilRetentionSweepsIt() {
    YearMonth month = YearMonth.of(2024, 3);
    fakeChessClient.addGame(PLAYER, month, "https://chess.com/game/retention-1");
    fakeChessClient.addGame(PLAYER, month, "https://chess.com/game/retention-2");

    IndexResponse created =
        controller.createIndex(new IndexRequest(PLAYER, PLATFORM, "2024-03", "2024-03", null));
    processQueueUntilIdle();

    IndexResponse fresh = controller.getIndex(created.id());
    assertThat(fresh.status()).isEqualTo("COMPLETED");
    assertThat(fresh.gamesIndexed()).isEqualTo(2);
    assertThat(fresh.data()).isNotNull();
    assertThat(fresh.data().status()).isEqualTo("AVAILABLE");
    assertThat(fresh.data().monthsAvailable()).isEqualTo(1);
    assertThat(fresh.data().monthsTotal()).isEqualTo(1);
    assertThat(fresh.data().expiresAt())
        .isNotNull()
        .isAfter(Instant.now().plus(RetentionPolicy.PERIOD).minus(Duration.ofMinutes(5)));

    // Run the real worker, having advanced past the retention boundary. Calling
    // deleteOlderThan with a hand-made threshold would only test the DELETE; this exercises
    // RetentionWorker's own `clock.instant().minus(RETENTION_PERIOD)` arithmetic, so changing
    // RetentionPolicy.PERIOD now breaks a test instead of silently changing behaviour.
    new RetentionWorker(
            gameFeatureStore, periodStore, requestStore, clockOffsetBy(RETENTION_PLUS_MARGIN))
        .runRetention();

    IndexResponse swept = controller.getIndex(created.id());
    // The request row is untouched — which is exactly why it needs the extra signal.
    assertThat(swept.status()).isEqualTo("COMPLETED");
    assertThat(swept.gamesIndexed()).isEqualTo(2);
    assertThat(swept.data()).isNotNull();
    assertThat(swept.data().status()).isEqualTo("EXPIRED");
    assertThat(swept.data().monthsAvailable()).isZero();
    assertThat(swept.data().expiresAt()).isNull();

    assertThat(controller.listRequests())
        .filteredOn(r -> r.id().equals(created.id()))
        .singleElement()
        .satisfies(r -> assertThat(r.data().status()).isEqualTo("EXPIRED"));
  }

  /**
   * One minute short of the window, nothing goes. The control for the sweep above: without it, a
   * RetentionWorker that deleted unconditionally would pass that test just as well.
   */
  @Test
  public void retentionLeavesDataAloneUntilTheWindowActuallyElapses() {
    YearMonth month = YearMonth.of(2024, 3);
    fakeChessClient.addGame(PLAYER, month, "https://chess.com/game/not-yet-1");

    IndexResponse created =
        controller.createIndex(new IndexRequest(PLAYER, PLATFORM, "2024-03", "2024-03", null));
    processQueueUntilIdle();

    new RetentionWorker(
            gameFeatureStore, periodStore, requestStore, clockOffsetBy(RETENTION_MINUS_MARGIN))
        .runRetention();

    assertThat(controller.getIndex(created.id()).data().status()).isEqualTo("AVAILABLE");
  }

  /**
   * A period must never outlive the games it vouches for. game_features.indexed_at is stamped by
   * the database as each batch lands; indexed_periods.fetched_at is taken before the month's work
   * begins, so the period is the older row and retention removes it first. Stamping it afterwards
   * would leave a window reporting AVAILABLE over games that are already gone.
   */
  @Test
  public void aPeriodIsNeverNewerThanTheGamesItVouchesFor() {
    YearMonth month = YearMonth.of(2024, 3);
    for (int i = 0; i < 3; i++) {
      fakeChessClient.addGame(PLAYER, month, "https://chess.com/game/skew-" + i);
    }

    controller.createIndex(new IndexRequest(PLAYER, PLATFORM, "2024-03", "2024-03", null));
    processQueueUntilIdle();

    Instant fetchedAt =
        periodStore
            .findCompletePeriod(PLAYER, PLATFORM, "2024-03", false)
            .orElseThrow()
            .fetchedAt();
    Instant earliestGame =
        gameFeatureStore
            .query(new SqlCompiler().compile(Parser.parse("num.moves >= 0")), 100, 0)
            .stream()
            .map(GameFeature::indexedAt)
            .min(Instant::compareTo)
            .orElseThrow();

    assertThat(fetchedAt).isBeforeOrEqualTo(earliestGame);
  }

  private static final Duration MARGIN = Duration.ofMinutes(5);

  /**
   * Stated as a literal, deliberately. Deriving these offsets from {@link RetentionPolicy#PERIOD}
   * makes the boundary tests move with the constant, so changing 7 days to 700 leaves them green —
   * a test that pins a value to itself. The literal is the test's independent claim about what the
   * window is; {@link #retentionWindowIsSevenDays} is what reports the mismatch legibly when
   * someone changes it on purpose.
   */
  private static final Duration SEVEN_DAYS = Duration.ofDays(7);

  /** Threshold lands MARGIN *after* the rows just written, so they are old enough to sweep. */
  private static final Duration RETENTION_PLUS_MARGIN = SEVEN_DAYS.plus(MARGIN);

  /** Threshold lands MARGIN *before* them, so the window has not elapsed yet. */
  private static final Duration RETENTION_MINUS_MARGIN = SEVEN_DAYS.minus(MARGIN);

  /**
   * The window is published to users in two places nothing else gates — API.md's "**7 days**" and
   * the web app's "kept for 7 days" panel note. Changing the constant has to fail here so those get
   * updated with it.
   */
  @Test
  public void retentionWindowIsSevenDays() {
    assertThat(RetentionPolicy.PERIOD).isEqualTo(SEVEN_DAYS);
  }

  /**
   * Same contract, the other two windows. Both are published in prose that nothing else gates —
   * API.md's "**30 days**" and its "23-day gap" paragraph, the README retention table, and the web
   * app's panel note. Behavior alone pins these only loosely: the worker suite's fixtures are 10
   * and 40 days old, so anything in between would keep it green while falsifying every one of those
   * sentences.
   */
  @Test
  public void requestRetentionWindowIsThirtyDays() {
    assertThat(RetentionPolicy.REQUEST).isEqualTo(Duration.ofDays(30));
  }

  @Test
  public void strandedRequestCutoffIsOneHour() {
    assertThat(RetentionPolicy.STALE_REQUEST).isEqualTo(Duration.ofHours(1));
  }

  /**
   * Not a style preference — a correctness constraint. {@code game_features.request_id} is a
   * foreign key onto {@code indexing_requests(id)}, so a request must outlive the games it produced
   * or the sweep would be deleting rows its own children still reference. Asserted here rather than
   * in a static initializer on RetentionPolicy, where a violation would surface as an
   * ExceptionInInitializerError during Micronaut startup instead of as a failing test.
   */
  @Test
  public void requestsAreRetainedLongerThanTheGamesTheyProduced() {
    assertThat(RetentionPolicy.REQUEST).isGreaterThan(RetentionPolicy.PERIOD);
  }

  @Test
  public void leaseIsFiveMinutes() {
    assertThat(RetentionPolicy.LEASE).isEqualTo(Duration.ofMinutes(5));
  }

  /**
   * The relationship that makes a lease work at all, and the one most easily broken by accident. A
   * renewal interval at or above the lease means every lease lapses between beats — which does not
   * fail loudly, it fails as healthy workers having their ranges reclaimed underneath them.
   *
   * <p>This exact mistake was live in an earlier draft of #1278: the heartbeat kept its old pacing
   * off {@link RetentionPolicy#STALE_REQUEST} (a quarter of an hour) while the new lease was five
   * minutes, so a worker would have lost its claim three times over between one beat and the next.
   */
  @Test
  public void theLeaseIsRenewedSeveralTimesBeforeItCouldExpire() {
    // Asserted on DEFAULT_HEARTBEAT_INTERVAL, which is what a change would actually touch.
    // LEASE_RENEWAL is defined as LEASE.dividedBy(4), so any claim relating those two — including
    // "three renewals fit inside a lease" — is arithmetic, true for every positive LEASE, and
    // fails for nothing. The heartbeat interval is an independent constant that has already been
    // pointed at the wrong policy once.
    assertThat(IndexWorker.DEFAULT_HEARTBEAT_INTERVAL.multipliedBy(3))
        .as("at least three renewals may be lost before anyone else may take the request")
        .isLessThan(RetentionPolicy.LEASE);
  }

  /**
   * The two clocks are answers to different questions and must not converge. The hour is for work
   * nobody has claimed, where the only evidence is the row's age; the lease is for work someone
   * has, where the owner itself reports in. If the lease ever grew to the staleness cutoff the
   * distinction would be decorative.
   */
  @Test
  public void aDeadOwnerIsReclaimedFarSoonerThanAnUnclaimedRow() {
    assertThat(RetentionPolicy.LEASE).isLessThan(RetentionPolicy.STALE_REQUEST);
  }

  /**
   * And the third clock sits above both (#1282). It is the only one that can end a run whose worker
   * is alive but not progressing, so it must be long enough that reaching it is evidence of a fault
   * rather than of a large request.
   *
   * <p>The lower bound is the load-bearing one. A run released at the ceiling hands its row to a
   * replacement carrying an {@code updated_at} as old as the run was, so a ceiling under {@link
   * RetentionPolicy#STALE_REQUEST} would deliver work that is already eligible for the stalled arm
   * — the replacement would be racing a sweep that thinks nobody is serving the request. Nothing
   * about that fails loudly; it surfaces as requests failing shortly after being requeued.
   */
  @Test
  public void aRunMayOutlastEveryOtherWindowBeforeItsCeilingApplies() {
    assertThat(RetentionPolicy.MAX_RUN).isGreaterThan(RetentionPolicy.STALE_REQUEST);
    assertThat(RetentionPolicy.MAX_RUN).isGreaterThan(RetentionPolicy.LEASE);
    assertThat(RetentionPolicy.MAX_RUN).isEqualTo(Duration.ofHours(6));
  }

  /**
   * A clock parked {@code offset} into the future. RetentionWorker subtracts RetentionPolicy.PERIOD
   * from it, so the resulting threshold sits {@code offset - PERIOD} either side of "now" — which
   * is what puts the rows just written on one side of the boundary or the other.
   */
  private static java.time.Clock clockOffsetBy(Duration offset) {
    return java.time.Clock.fixed(Instant.now().plus(offset), java.time.ZoneOffset.UTC);
  }

  @Test
  public void
      createIndex_duplicateParamsWhilePending_returnsExistingRequestAndDoesNotEnqueueSecond() {
    YearMonth month = YearMonth.of(2024, 4);
    fakeChessClient.addGame(PLAYER, month, "https://chess.com/game/dup-1");

    IndexRequest request = new IndexRequest(PLAYER, PLATFORM, "2024-04", "2024-04", null);
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
        PLAYER, PLATFORM, "2024-01", Instant.parse("2024-02-01T00:00:00Z"), true, 3, false);

    fakeChessClient.setNoGames(PLAYER, jan);
    fakeChessClient.addGame(PLAYER, feb, "https://chess.com/game/feb-1");

    IndexRequest request = new IndexRequest(PLAYER, PLATFORM, "2024-01", "2024-02", null);
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
        PLAYER, PLATFORM, "2024-02", Instant.parse("2024-03-01T00:00:00Z"), true, 5, false);

    fakeChessClient.addGame(PLAYER, jan, "https://chess.com/game/jan-1");
    fakeChessClient.addGame(PLAYER, jan, "https://chess.com/game/jan-2");
    fakeChessClient.setNoGames(PLAYER, feb);
    fakeChessClient.addGame(PLAYER, mar, "https://chess.com/game/mar-1");

    IndexRequest request = new IndexRequest(PLAYER, PLATFORM, "2024-01", "2024-03", null);
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

    IndexRequest request = new IndexRequest(PLAYER, PLATFORM, "2024-05", "2024-05", null);
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

    IndexRequest request = new IndexRequest(PLAYER, PLATFORM, "2024-06", "2024-06", null);
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

  @Test
  public void createIndex_withExcludeBullet_filtersBulletGames() {
    YearMonth month = YearMonth.of(2024, 7);
    fakeChessClient.addGame(PLAYER, month, "https://chess.com/game/blitz-e2e-1");
    fakeChessClient.setGames(
        PLAYER,
        month,
        List.of(
            FakeChessClient.minimalPlayedGame("https://chess.com/game/blitz-e2e-1"),
            FakeChessClient.bulletPlayedGame("https://chess.com/game/bullet-e2e-1")));

    IndexRequest request = new IndexRequest(PLAYER, PLATFORM, "2024-07", "2024-07", true);
    IndexResponse created = controller.createIndex(request);
    assertThat(created.excludeBullet()).isTrue();

    processQueueUntilIdle();

    IndexResponse after = controller.getIndex(created.id());
    assertThat(after.status()).isEqualTo("COMPLETED");
    assertThat(after.gamesIndexed()).isEqualTo(1); // bullet game excluded
    assertThat(after.excludeBullet()).isTrue();
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
