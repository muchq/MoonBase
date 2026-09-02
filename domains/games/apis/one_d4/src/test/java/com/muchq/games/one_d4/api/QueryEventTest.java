package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.ParseException;
import com.muchq.games.one_d4.api.dto.AggregateRequest;
import com.muchq.games.one_d4.api.dto.QueryRequest;
import com.muchq.games.one_d4.api.dto.QueryResponse;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.slf4j.LoggerFactory;
import org.slf4j.event.KeyValuePair;

/**
 * The query event (#1465) as the controllers emit it: one line per request on its own logger, the
 * fields the stats pipeline reads, and nothing the caller wrote. Captured at the logback event
 * rather than the JSON line — LogbackConfigTest pins that key-value pairs reach the JSON.
 */
public class QueryEventTest {

  private static final String BROWSER =
      "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 Chrome/126.0";

  private ListAppender<ILoggingEvent> captured;
  private FakeGameFeatureStore store;
  private QueryController queryController;
  private AggregateController aggregateController;

  @BeforeEach
  public void setUp() {
    captured = new ListAppender<>();
    captured.start();
    logger().addAppender(captured);

    store = new FakeGameFeatureStore();
    QueryExecutor executor = new QueryExecutor(store, new SqlCompiler());
    FirstPageCache cache =
        new FirstPageCache(new MutableTicker(), FirstPageCache.MAX_AGE, executor);
    queryController = new QueryController(executor, new QueryRequestValidator(), cache);
    aggregateController =
        new AggregateController(store, new SqlCompiler(), new AggregateRequestValidator());
  }

  @AfterEach
  public void tearDown() {
    logger().detachAppender(captured);
  }

  private static Logger logger() {
    return (Logger) LoggerFactory.getLogger(QueryEvent.LOGGER);
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

  @Test
  public void aLiveQueryFromTheWebAppLogsItsShapeAndOutcome() {
    QueryResponse response =
        queryController.query(
            new QueryRequest(
                "white.elo > 2500 AND motif(fork) ORDER BY motif_count(fork) DESC", 10, 5),
            BROWSER,
            "https://1d4.net");

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
        .containsEntry("rows", String.valueOf(response.count()))
        .containsEntry("outcome", "ok")
        .containsKey("duration_us");
    assertThat(Long.parseLong(fields.get("duration_us"))).isNotNegative();
    // The value in the query is the caller's; it must not be in the line.
    assertThat(fields.values()).noneMatch(value -> value.contains("2500"));
    assertThat(fields.keySet())
        .containsExactly(
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
  }

  @Test
  public void theDefaultRequestIsServedFromTheSnapshot() {
    queryController.query(
        new QueryRequest(FirstPageCache.DEFAULT_QUERY, FirstPageCache.DEFAULT_LIMIT, 0, null),
        BROWSER,
        "https://1d4.net");

    assertThat(theOneEvent()).containsEntry("cache", "snapshot").containsEntry("outcome", "ok");
  }

  @Test
  public void aPlayerIsRecordedAsPresentNeverByName() {
    queryController.query(
        new QueryRequest("outcome = \"win\"", 10, 0, "hikaru"), "curl/8.6.0", null);

    Map<String, String> fields = theOneEvent();
    assertThat(fields).containsEntry("player", "true").containsEntry("source", "api");
    assertThat(fields.values()).noneMatch(value -> value.contains("hikaru"));
  }

  @Test
  public void aRequestThatFailsValidationIsInvalidAndStillThrows() {
    assertThatThrownBy(() -> queryController.query(new QueryRequest("", 10, 0), null, null))
        .isInstanceOf(IllegalArgumentException.class);

    Map<String, String> fields = theOneEvent();
    assertThat(fields).containsEntry("outcome", "invalid").doesNotContainKey("fields");
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
    QueryExecutor broken =
        new QueryExecutor(store, new SqlCompiler()) {
          @Override
          public QueryResponse execute(QueryRequest request) {
            throw new IllegalStateException("database is having a day");
          }
        };
    QueryController controller =
        new QueryController(
            broken,
            new QueryRequestValidator(),
            new FirstPageCache(new MutableTicker(), FirstPageCache.MAX_AGE, broken));

    assertThatThrownBy(() -> controller.query(new QueryRequest("motif(pin)", 10, 0), null, null))
        .isInstanceOf(IllegalStateException.class);

    assertThat(theOneEvent()).containsEntry("outcome", "failed").containsEntry("cache", "live");
  }

  @Test
  public void anAggregateLogsItsGroupingAndRanking() {
    aggregateController.aggregate(
        new AggregateRequest(
            "outcome = \"win\" AND white.elo > 2000",
            List.of("eco", "opening.family"),
            "score",
            20,
            "hikaru",
            5),
        OneD4ClientAgent.MCPSERVER,
        null);

    Map<String, String> fields = theOneEvent();
    assertThat(fields)
        .containsEntry("entry", "aggregate")
        .containsEntry("source", "mcp")
        .containsEntry("fields", "outcome,white.elo")
        .containsEntry("player", "true")
        .containsEntry("group_by", "eco,opening.family")
        .containsEntry("order", "score")
        .containsEntry("min_games", "5")
        .containsEntry("limit", "20")
        .containsEntry("outcome", "ok")
        .containsKey("rows");
    assertThat(fields.values()).noneMatch(value -> value.contains("hikaru"));
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
        .containsEntry("outcome", "invalid");
  }

  @Test
  public void sourceIsMcpByUserAgentThenUiByOriginThenApi() {
    assertThat(QueryEvent.sourceOf(OneD4ClientAgent.MCPSERVER, null)).isEqualTo("mcp");
    // mcpserver never sends an Origin; if something did, the agent still decides.
    assertThat(QueryEvent.sourceOf(OneD4ClientAgent.MCPSERVER, "https://1d4.net")).isEqualTo("mcp");
    assertThat(QueryEvent.sourceOf(BROWSER, "https://1d4.net")).isEqualTo("ui");
    assertThat(QueryEvent.sourceOf(BROWSER, "http://localhost:5173")).isEqualTo("ui");
    assertThat(QueryEvent.sourceOf(BROWSER, "https://evil.example")).isEqualTo("api");
    assertThat(QueryEvent.sourceOf(BROWSER, null)).isEqualTo("api");
    assertThat(QueryEvent.sourceOf("curl/8.6.0", null)).isEqualTo("api");
    assertThat(QueryEvent.sourceOf(null, null)).isEqualTo("api");
    // A UA that merely mentions mcpserver is not it: the token leads.
    assertThat(QueryEvent.sourceOf("Mozilla/5.0 (compatible; not-mcpserver)", null))
        .isEqualTo("api");
  }

  /** What mcpserver's OneD4Client sends, spelled here so the two sides cannot drift unnoticed. */
  private static final class OneD4ClientAgent {
    static final String MCPSERVER = "mcpserver";
  }
}
