package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.ParseException;
import com.muchq.games.one_d4.api.dto.AggregateRequest;
import com.muchq.games.one_d4.api.dto.AggregateResponse;
import com.muchq.games.one_d4.api.dto.AggregateRow;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.QueryRequest;
import com.muchq.games.one_d4.api.dto.QueryResponse;
import com.muchq.games.one_d4.db.GameFeatureStore.AggregateTotals;
import com.muchq.platform.yodel.CustomMetrics;
import java.time.Instant;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.NoSuchElementException;
import java.util.UUID;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.slf4j.LoggerFactory;
import org.slf4j.event.KeyValuePair;

/**
 * The query event (#1465) as the controllers emit it: one line per request on its own logger, the
 * fields the stats pipeline reads, the counter and histogram beside them, and nothing the caller
 * wrote. Captured at the logback event rather than the JSON line — LogbackConfigTest pins that
 * key-value pairs reach the JSON.
 */
public class QueryEventTest {

  private static final String BROWSER =
      "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 Chrome/126.0";
  private static final String UI = "https://1d4.net";

  private ListAppender<ILoggingEvent> captured;
  private ListAppender<ILoggingEvent> everything;
  private FakeGameFeatureStore store;
  private CustomMetrics metrics;
  private QueryEvents events;
  private QueryController queryController;
  private AggregateController aggregateController;

  @BeforeEach
  public void setUp() {
    captured = new ListAppender<>();
    captured.start();
    eventLogger().addAppender(captured);
    everything = new ListAppender<>();
    everything.start();
    rootLogger().addAppender(everything);

    store = new FakeGameFeatureStore();
    metrics = TestQueryEvents.metrics();
    events = new QueryEvents(metrics);
    QueryExecutor executor = new QueryExecutor(store, new SqlCompiler());
    FirstPageCache cache =
        new FirstPageCache(new MutableTicker(), FirstPageCache.MAX_AGE, executor);
    queryController = new QueryController(executor, new QueryRequestValidator(), cache, events);
    aggregateController =
        new AggregateController(store, new SqlCompiler(), new AggregateRequestValidator(), events);
  }

  @AfterEach
  public void tearDown() {
    eventLogger().detachAppender(captured);
    rootLogger().detachAppender(everything);
  }

  private static Logger eventLogger() {
    return (Logger) LoggerFactory.getLogger(QueryEvent.LOGGER);
  }

  private static Logger rootLogger() {
    return (Logger) LoggerFactory.getLogger(Logger.ROOT_LOGGER_NAME);
  }

  private Map<String, String> theOneEvent() {
    assertThat(captured.list).as("exactly one event per request").hasSize(1);
    ILoggingEvent event = captured.list.get(0);
    assertThat(event.getMessage()).isEqualTo(QueryEvent.MESSAGE);
    assertThat(event.getLoggerName()).isEqualTo(QueryEvent.LOGGER);
    Map<String, String> fields = new LinkedHashMap<>();
    for (KeyValuePair pair : event.getKeyValuePairs()) {
      fields.put(pair.key, String.valueOf(pair.value));
    }
    return fields;
  }

  /** The values a caller could have authored; duration is the clock's, not theirs. */
  private static List<String> callerVisibleValues(Map<String, String> fields) {
    return fields.entrySet().stream()
        .filter(field -> !field.getKey().equals("duration_us"))
        .map(Map.Entry::getValue)
        .toList();
  }

  private long counter(String entry, String source, String outcome, String cache) {
    Map<String, String> labels = QueryEvents.labels(entry, source, outcome, cache);
    return metrics.counterSnapshot().stream()
        .filter(c -> c.name().equals(QueryEvents.QUERIES) && c.labels().equals(labels))
        .mapToLong(CustomMetrics.CounterSnapshot::value)
        .findFirst()
        .orElseThrow(() -> new AssertionError("no counter series " + labels));
  }

  private static GameFeature aGame(String url) {
    return new GameFeature(
        UUID.randomUUID(),
        UUID.randomUUID(),
        url,
        "CHESS_COM",
        "white",
        "black",
        2000,
        1900,
        null,
        "GM",
        "blitz",
        "B90",
        "Sicilian Defense Najdorf Variation",
        "Sicilian Defense",
        "1-0",
        Instant.now(),
        30,
        Instant.now(),
        "pgn");
  }

  @Test
  public void aLiveQueryFromTheWebAppLogsItsShapeAndOutcomeAndCountsItself() {
    store.setQueryResult(
        List.of(aGame("https://chess.com/game/1"), aGame("https://chess.com/game/2")));

    QueryResponse response =
        queryController.query(
            new QueryRequest(
                "white.elo > 2500 AND motif(fork) ORDER BY motif_count(fork) DESC", 10, 5),
            BROWSER,
            UI);

    assertThat(response.count()).isEqualTo(2);
    Map<String, String> fields = theOneEvent();
    assertThat(fields)
        .containsEntry("entry", "query")
        .containsEntry("source", "ui")
        .containsEntry("fields", "white.elo")
        .containsEntry("motifs", "fork")
        .containsEntry("order_by", "fork")
        .containsEntry("player", "false")
        .containsEntry("limit", "10")
        .containsEntry("offset", "5")
        .containsEntry("cache", "live")
        .containsEntry("rows", "2")
        .containsEntry("outcome", "ok");
    assertThat(Long.parseLong(fields.get("duration_us"))).isNotNegative();
    // The value in the query is the caller's; it must not be in the line.
    assertThat(callerVisibleValues(fields)).noneMatch(value -> value.contains("2500"));
    assertThat(fields.keySet())
        .containsExactlyInAnyOrder(
            "entry",
            "source",
            "fields",
            "motifs",
            "order_by",
            "player",
            "limit",
            "offset",
            "cache",
            "rows",
            "outcome",
            "duration_us");

    assertThat(counter("query", "ui", "ok", "live")).isEqualTo(1);
    assertThat(
            metrics.distributionSnapshot().stream()
                .filter(
                    d ->
                        d.name().equals(QueryEvents.DURATION)
                            && d.labels().equals(Map.of("entry", "query", "source", "ui")))
                .mapToLong(CustomMetrics.DistributionSnapshot::count)
                .sum())
        .isEqualTo(1);
  }

  /** Every bounded series exists from boot, so the first request is counted, not baselined. */
  @Test
  public void everyCounterSeriesIsDeclaredBeforeTheFirstRequest() {
    for (String source : QueryEvents.SOURCES) {
      for (String outcome : QueryEvents.OUTCOMES) {
        for (String cache : List.of("snapshot", "live", "none")) {
          assertThat(counter("query", source, outcome, cache)).isZero();
        }
        assertThat(counter("aggregate", source, outcome, "none")).isZero();
      }
    }
  }

  /**
   * "snapshot" means served through the first-page snapshot, whether that was a hit or the load
   * that warmed it — the store sees one query for two such requests.
   */
  @Test
  public void theDefaultRequestIsServedFromTheSnapshotWarmOrCold() {
    QueryRequest first =
        new QueryRequest(FirstPageCache.DEFAULT_QUERY, FirstPageCache.DEFAULT_LIMIT, 0, null);

    queryController.query(first, BROWSER, UI);
    queryController.query(first, BROWSER, UI);

    assertThat(store.queryCount()).isEqualTo(1);
    assertThat(captured.list).hasSize(2);
    for (ILoggingEvent event : captured.list) {
      assertThat(event.getKeyValuePairs())
          .anyMatch(pair -> pair.key.equals("cache") && pair.value.equals("snapshot"));
    }
    assertThat(counter("query", "ui", "ok", "snapshot")).isEqualTo(2);
  }

  @Test
  public void aPlayerIsRecordedAsPresentNeverByName() {
    queryController.query(
        new QueryRequest("outcome = \"win\"", 10, 0, "hikaru"), "curl/8.6.0", null);

    Map<String, String> fields = theOneEvent();
    assertThat(fields).containsEntry("player", "true").containsEntry("source", "api");
    assertThat(fields.values()).noneMatch(value -> value.contains("hikaru"));
  }

  /** A blank player is no player: the cache and the compiler both read it that way. */
  @Test
  public void aBlankPlayerIsNoPlayer() {
    queryController.query(
        new QueryRequest(FirstPageCache.DEFAULT_QUERY, FirstPageCache.DEFAULT_LIMIT, 0, "   "),
        BROWSER,
        UI);

    assertThat(theOneEvent()).containsEntry("player", "false").containsEntry("cache", "snapshot");
  }

  /** The query text is the caller's; no logger carries it. */
  @Test
  public void theQueryTextIsLoggedNowhere() {
    queryController.query(
        new QueryRequest("white.username = \"zq7distinctive\" AND motif(pin)", 10, 0), BROWSER, UI);

    assertThat(captured.list).hasSize(1);
    assertThat(everything.list).isNotEmpty();
    for (ILoggingEvent event : everything.list) {
      assertThat(event.getFormattedMessage()).doesNotContain("zq7distinctive");
      List<KeyValuePair> pairs =
          event.getKeyValuePairs() == null ? List.of() : event.getKeyValuePairs();
      for (KeyValuePair pair : pairs) {
        assertThat(String.valueOf(pair.value)).doesNotContain("zq7distinctive");
      }
    }
  }

  @Test
  public void aRequestThatFailsValidationIsInvalidAndStillThrows() {
    assertThatThrownBy(() -> queryController.query(new QueryRequest("", 10, 0), null, null))
        .isInstanceOf(IllegalArgumentException.class);

    Map<String, String> fields = theOneEvent();
    assertThat(fields).containsEntry("outcome", "invalid").doesNotContainKey("fields");
    assertThat(counter("query", "api", "invalid", "none")).isEqualTo(1);
  }

  @Test
  public void aQueryThatDoesNotParseIsInvalid() {
    assertThatThrownBy(
            () -> queryController.query(new QueryRequest("played.at = NULL", 10, 0), null, null))
        .isInstanceOf(ParseException.class);

    assertThat(theOneEvent()).containsEntry("outcome", "invalid").doesNotContainKey("fields");
  }

  @Test
  public void aQueryTheServiceCannotAnswerIsFailedNotInvalid() {
    store.failQueriesWith(new IllegalStateException("database is having a day"));

    assertThatThrownBy(
            () -> queryController.query(new QueryRequest("motif(pin)", 10, 0), null, null))
        .isInstanceOf(IllegalStateException.class);

    assertThat(theOneEvent()).containsEntry("outcome", "failed").containsEntry("cache", "live");
    assertThat(counter("query", "api", "failed", "live")).isEqualTo(1);
  }

  /** The error handler answers a NoSuchElementException with 404; the event calls it failed. */
  @Test
  public void aNotFoundIsFailedNotInvalid() {
    store.failQueriesWith(new NoSuchElementException("gone"));

    assertThatThrownBy(
            () -> queryController.query(new QueryRequest("motif(pin)", 10, 0), null, null))
        .isInstanceOf(NoSuchElementException.class);

    assertThat(theOneEvent()).containsEntry("outcome", "failed");
  }

  /** An Error unwinding the handler still leaves exactly one event, and still propagates. */
  @Test
  public void anErrorUnwindingTheHandlerIsFailedAndRethrown() {
    QueryExecutor broken =
        new QueryExecutor(store, new SqlCompiler()) {
          @Override
          public QueryResponse execute(QueryRequest request) {
            throw new StackOverflowError();
          }
        };
    QueryController controller =
        new QueryController(
            broken,
            new QueryRequestValidator(),
            new FirstPageCache(new MutableTicker(), FirstPageCache.MAX_AGE, broken),
            events);

    assertThatThrownBy(() -> controller.query(new QueryRequest("motif(pin)", 10, 0), null, null))
        .isInstanceOf(StackOverflowError.class);

    assertThat(theOneEvent()).containsEntry("outcome", "failed");
  }

  @Test
  public void anAggregateLogsItsGroupingAndRankingAndFillsTheTotalsBranch() {
    // Two groups against a limit of two: the totals branch runs, and rows is the group count.
    store.setAggregateResult(
        List.of(
            new AggregateRow(Map.of("eco", "B90"), 7), new AggregateRow(Map.of("eco", "B91"), 3)),
        new AggregateTotals(15, 4));

    AggregateResponse response =
        aggregateController.aggregate(
            new AggregateRequest(
                "outcome = \"win\" AND white.elo > 2000",
                List.of("eco", "opening.family"),
                "score",
                2,
                "hikaru",
                5),
            QueryEvent.MCPSERVER_AGENT + "/1",
            null);

    assertThat(response.truncated()).isTrue();
    Map<String, String> fields = theOneEvent();
    assertThat(fields)
        .containsEntry("entry", "aggregate")
        .containsEntry("source", "mcp")
        .containsEntry("fields", "outcome,white.elo")
        .containsEntry("player", "true")
        // The compiler's resolved column names, not the request's spelling.
        .containsEntry("group_by", "eco,opening_family")
        .containsEntry("order", "score")
        .containsEntry("min_games", "5")
        .containsEntry("limit", "2")
        .containsEntry("rows", "2")
        .containsEntry("outcome", "ok");
    assertThat(fields.keySet())
        .containsExactlyInAnyOrder(
            "entry",
            "source",
            "fields",
            "motifs",
            "order_by",
            "player",
            "group_by",
            "order",
            "min_games",
            "limit",
            "rows",
            "outcome",
            "duration_us");
    assertThat(callerVisibleValues(fields)).noneMatch(value -> value.contains("hikaru"));
    assertThat(counter("aggregate", "mcp", "ok", "none")).isEqualTo(1);
  }

  @Test
  public void anAggregateTheValidatorRejectsIsInvalid() {
    assertThatThrownBy(
            () ->
                aggregateController.aggregate(
                    new AggregateRequest("white.elo > 2000", List.of(), null, 20), null, null))
        .isInstanceOf(IllegalArgumentException.class);

    assertThat(theOneEvent())
        .containsEntry("entry", "aggregate")
        .containsEntry("outcome", "invalid")
        .doesNotContainKey("group_by");
  }

  /** Group-by names are logged only after the compiler has accepted them. */
  @Test
  public void anUnknownGroupByNeverReachesTheLine() {
    assertThatThrownBy(
            () ->
                aggregateController.aggregate(
                    new AggregateRequest("white.elo > 2000", List.of("zq7notacolumn"), null, 20),
                    null,
                    null))
        .isInstanceOf(IllegalArgumentException.class);

    Map<String, String> fields = theOneEvent();
    assertThat(fields).containsEntry("outcome", "invalid").doesNotContainKey("group_by");
    assertThat(fields.values()).noneMatch(value -> value.contains("zq7notacolumn"));
  }

  @Test
  public void sourceIsMcpByUserAgentThenUiByOriginThenApi() {
    // What mcpserver's OneD4Client sends; IndexerFacadeHttpTest pins the same literal there.
    assertThat(QueryEvent.MCPSERVER_AGENT).isEqualTo("mcpserver");
    assertThat(QueryEvent.sourceOf("mcpserver", null)).isEqualTo("mcp");
    // mcpserver never sends an Origin; if something did, the agent still decides.
    assertThat(QueryEvent.sourceOf("mcpserver", UI)).isEqualTo("mcp");
    assertThat(QueryEvent.sourceOf(BROWSER, UI)).isEqualTo("ui");
    assertThat(QueryEvent.sourceOf(BROWSER, "http://localhost:5173")).isEqualTo("ui");
    assertThat(QueryEvent.sourceOf(BROWSER, "https://evil.example")).isEqualTo("api");
    assertThat(QueryEvent.sourceOf(BROWSER, null)).isEqualTo("api");
    assertThat(QueryEvent.sourceOf("curl/8.6.0", null)).isEqualTo("api");
    assertThat(QueryEvent.sourceOf(null, null)).isEqualTo("api");
    // A UA that merely mentions mcpserver is not it: the token leads.
    assertThat(QueryEvent.sourceOf("Mozilla/5.0 (compatible; not-mcpserver)", null))
        .isEqualTo("api");
  }
}
