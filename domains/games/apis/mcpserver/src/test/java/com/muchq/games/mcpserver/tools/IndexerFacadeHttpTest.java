package com.muchq.games.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.one_d4.api.dto.AnalyzeResponse;
import com.muchq.games.one_d4.api.dto.GameFeatureRow;
import com.muchq.games.one_d4.api.dto.IndexResponse;
import com.muchq.platform.http_client.jdk.Jdk11HttpClient;
import com.muchq.platform.json.JsonUtils;
import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * The MCP server as an HTTP adapter over one_d4 (#1332).
 *
 * <p>Against a real HTTP server rather than a stubbed client, because the things that break here
 * are wire-shaped: a path typo, a field name that does not match one_d4's DTO, a status code mapped
 * to the wrong kind of error. A fake at the Java-interface level would agree with whatever this
 * code believes and prove none of it.
 *
 * <p>What this file does <em>not</em> test is what indexing and aggregation mean — that lives in
 * one_d4 now, with its own tests. This tests the seam.
 */
public class IndexerFacadeHttpTest {

  private HttpServer server;
  private String baseUrl;
  private final List<String> requestLog = new ArrayList<>();
  private final Map<String, Handler> routes = new java.util.HashMap<>();

  interface Handler {
    void handle(HttpExchange exchange) throws IOException;
  }

  @BeforeEach
  public void startServer() throws Exception {
    server = HttpServer.create(new InetSocketAddress("localhost", 0), 0);
    server.createContext(
        "/",
        exchange -> {
          String key = exchange.getRequestMethod() + " " + exchange.getRequestURI().getPath();
          requestLog.add(key);
          Handler handler = routes.get(key);
          if (handler == null) {
            respond(exchange, 404, "{\"error\":\"no route for " + key + "\"}");
            return;
          }
          handler.handle(exchange);
        });
    server.start();
    baseUrl = "http://localhost:" + server.getAddress().getPort();
  }

  @AfterEach
  public void stopServer() {
    server.stop(0);
  }

  private void route(String key, int status, String body) {
    routes.put(key, exchange -> respond(exchange, status, body));
  }

  private static void respond(HttpExchange exchange, int status, String body) throws IOException {
    byte[] bytes = body.getBytes(StandardCharsets.UTF_8);
    exchange.getResponseHeaders().add("Content-Type", "application/json");
    exchange.sendResponseHeaders(status, bytes.length);
    exchange.getResponseBody().write(bytes);
    exchange.close();
  }

  private static String readBody(HttpExchange exchange) throws IOException {
    return new String(exchange.getRequestBody().readAllBytes(), StandardCharsets.UTF_8);
  }

  private IndexerFacade facade() {
    return facade(Duration.ofSeconds(5), Duration.ofMillis(1));
  }

  private IndexerFacade facade(Duration timeout, Duration interval) {
    OneD4Client client =
        new OneD4Client(
            new Jdk11HttpClient(java.net.http.HttpClient.newHttpClient()),
            JsonUtils.mapper(),
            baseUrl);
    // A no-op sleeper: these tests are about what the polling loop decides, not about it waiting.
    return new IndexerFacade(client, timeout, interval, millis -> {});
  }

  private static String indexJson(UUID id, String status, int games) {
    return "{\"id\":\""
        + id
        + "\",\"player\":\"hikaru\",\"platform\":\"CHESS_COM\",\"startMonth\":\"2026-06\","
        + "\"endMonth\":\"2026-06\",\"status\":\""
        + status
        + "\",\"gamesIndexed\":"
        + games
        + ",\"excludeBullet\":false}";
  }

  // ---- paths and JSON mapping -------------------------------------------------------------

  @Test
  public void queryPostsToTheQueryEndpointAndMapsTheResponse() {
    route(
        "POST /v1/query",
        200,
        "{\"games\":[{\"gameUrl\":\"https://chess.com/g/1\",\"whiteUsername\":\"hikaru\"}],"
            + "\"count\":1}");

    List<GameFeatureRow> games = facade().query("white.elo > 2700", "hikaru", 10);

    assertThat(requestLog).containsExactly("POST /v1/query");
    assertThat(games).hasSize(1);
    assertThat(games.get(0).gameUrl()).isEqualTo("https://chess.com/g/1");
  }

  /**
   * The request body has to use one_d4's field names, not the MCP tool's. Asserted on the bytes
   * because a rename on either side is silent otherwise: an unknown field is ignored by the
   * upstream mapper and the query simply runs unfiltered.
   */
  @Test
  public void theQueryRequestCarriesOneD4sFieldNames() throws Exception {
    List<String> bodies = new ArrayList<>();
    routes.put(
        "POST /v1/query",
        exchange -> {
          bodies.add(readBody(exchange));
          respond(exchange, 200, "{\"games\":[],\"count\":0}");
        });

    facade().query("motif(pin)", "hikaru", 7);

    var body = JsonUtils.mapper().readTree(bodies.get(0));
    assertThat(body.get("query").asText()).isEqualTo("motif(pin)");
    assertThat(body.get("player").asText()).isEqualTo("hikaru");
    assertThat(body.get("limit").asInt()).isEqualTo(7);
  }

  @Test
  public void aggregatePostsToTheAggregateEndpointAndKeepsTheUntruncatedTotals() {
    route(
        "POST /v1/aggregate",
        200,
        "{\"groups\":[{\"group\":{\"opening_family\":\"Sicilian Defense\"},\"count\":12}],"
            + "\"count\":1,\"totalGames\":40,\"totalGroups\":9,\"truncated\":true}");

    IndexerFacade.AggregateResult result =
        facade().aggregate("white.elo > 1", List.of("opening_family"), null, 1);

    assertThat(requestLog).containsExactly("POST /v1/aggregate");
    assertThat(result.groups()).hasSize(1);
    assertThat(result.totalGames()).isEqualTo(40);
    assertThat(result.totalGroups())
        .as("the untruncated total is what tells a caller a long tail was cut off")
        .isEqualTo(9);
  }

  /**
   * The other half of {@code aNonStringElementInAListArgumentIsHandledNotFatal}: a model can put a
   * JSON number in group_by, because the derived schema carries no {@code items} type to say
   * otherwise. The tool stringifies it, and what has to arrive upstream is the string — not a
   * dropped element, which would silently change the grouping into one the caller never asked for.
   */
  @Test
  public void aStringifiedGroupByElementReachesUpstreamIntact() throws Exception {
    List<String> bodies = new ArrayList<>();
    routes.put(
        "POST /v1/aggregate",
        exchange -> {
          bodies.add(readBody(exchange));
          respond(
              exchange,
              200,
              "{\"groups\":[],\"count\":0,\"totalGames\":0,\"totalGroups\":0,\"truncated\":false}");
        });

    facade().aggregate("white.elo > 1", List.of("opening_family", "5"), null, 20);

    var groupBy = JsonUtils.mapper().readTree(bodies.get(0)).get("groupBy");
    assertThat(groupBy).hasSize(2);
    assertThat(groupBy.get(1).asText()).isEqualTo("5");
  }

  /** The null group key the aggregate tool documents has to survive the extra hop. */
  @Test
  public void aNullGroupValueSurvivesTheRoundTrip() {
    route(
        "POST /v1/aggregate",
        200,
        "{\"groups\":[{\"group\":{\"opponent_title\":null},\"count\":3}],"
            + "\"count\":1,\"totalGames\":3,\"totalGroups\":1,\"truncated\":false}");

    IndexerFacade.AggregateResult result =
        facade().aggregate("me.color = \"white\"", List.of("opponent.title"), "hikaru", 20);

    assertThat(result.groups().get(0).group()).containsKey("opponent_title");
    assertThat(result.groups().get(0).group().get("opponent_title")).isNull();
  }

  @Test
  public void analyzePostsThePgnAndMapsTheMotifs() {
    route(
        "POST /v1/analyze",
        200,
        "{\"numMoves\":54,\"motifs\":[\"checkmate\"],\"occurrences\":{\"checkmate\":"
            + "[{\"ply\":107,\"moveNumber\":54,\"side\":\"white\",\"description\":\"mate\","
            + "\"isDiscovered\":false,\"isMate\":true}]}}");

    AnalyzeResponse response = facade().analyze("1. e4 e5");

    assertThat(requestLog).containsExactly("POST /v1/analyze");
    assertThat(response.numMoves()).isEqualTo(54);
    assertThat(response.motifs()).containsExactly("checkmate");
    assertThat(response.occurrences().get("checkmate").get(0).ply()).isEqualTo(107);
  }

  /** A blank PGN is refused here rather than spending a round trip to be told the same thing. */
  @Test
  public void aBlankPgnIsRejectedWithoutCallingUpstream() {
    assertThatThrownBy(() -> facade().analyze("  "))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("pgn is required");

    assertThat(requestLog).isEmpty();
  }

  @Test
  public void statusGetsTheRequestById() {
    UUID id = UUID.randomUUID();
    route("GET /v1/index/" + id, 200, indexJson(id, "COMPLETED", 325));

    Optional<IndexResponse> status = facade().status(id);

    assertThat(requestLog).containsExactly("GET /v1/index/" + id);
    assertThat(status).isPresent();
    assertThat(status.get().gamesIndexed()).isEqualTo(325);
  }

  /** An unknown request id is an empty result, not a failure — index_status reports "not found". */
  @Test
  public void anUnknownRequestIdIsEmptyRatherThanAnError() {
    UUID id = UUID.randomUUID();
    route("GET /v1/index/" + id, 404, "{\"error\":\"Indexing request not found\"}");

    assertThat(facade().status(id)).isEmpty();
  }

  // ---- polling ----------------------------------------------------------------------------

  /**
   * The behavior the in-process path used to get from {@code submitHybrid}: a single month comes
   * back finished, in one tool call. POST is async now, so the wait is polling — without it an
   * assistant would be told PENDING for a request that completes a moment later.
   */
  @Test
  public void aSingleMonthRequestIsPolledUntilItCompletes() {
    UUID id = UUID.randomUUID();
    route("POST /v1/index", 200, indexJson(id, "PENDING", 0));
    AtomicInteger polls = new AtomicInteger();
    routes.put(
        "GET /v1/index/" + id,
        exchange -> {
          int n = polls.incrementAndGet();
          respond(exchange, 200, indexJson(id, n < 3 ? "PROCESSING" : "COMPLETED", n < 3 ? 0 : 42));
        });

    IndexResponse response =
        facade().index("hikaru", "chess.com", "2026-06", "2026-06", false, false);

    assertThat(response.status()).isEqualTo("COMPLETED");
    assertThat(response.gamesIndexed()).isEqualTo(42);
    assertThat(polls.get()).isEqualTo(3);
  }

  /** FAILED is terminal too: polling must stop rather than spin until the budget runs out. */
  @Test
  public void pollingStopsOnFailure() {
    UUID id = UUID.randomUUID();
    route("POST /v1/index", 200, indexJson(id, "PENDING", 0));
    AtomicInteger polls = new AtomicInteger();
    routes.put(
        "GET /v1/index/" + id,
        exchange -> {
          polls.incrementAndGet();
          respond(exchange, 200, indexJson(id, "FAILED", 0));
        });

    IndexResponse response =
        facade().index("hikaru", "chess.com", "2026-06", "2026-06", false, false);

    assertThat(response.status()).isEqualTo("FAILED");
    assertThat(polls.get()).isEqualTo(1);
  }

  /**
   * A multi-month request is the case the async API exists for. Answering PENDING immediately is
   * the documented behavior, and polling it would block a tool call for as long as a year of games
   * takes to index.
   */
  @Test
  public void aMultiMonthRequestIsNotPolled() {
    UUID id = UUID.randomUUID();
    route("POST /v1/index", 200, indexJson(id, "PENDING", 0));

    IndexResponse response =
        facade().index("hikaru", "chess.com", "2026-01", "2026-06", false, false);

    assertThat(response.status()).isEqualTo("PENDING");
    assertThat(requestLog).containsExactly("POST /v1/index");
  }

  /**
   * Running out of polling budget is not an error. The request is still going upstream and its id
   * is in hand, so the caller gets the last status seen and can follow it with index_status —
   * throwing here would discard a working request id because one month was slow.
   */
  @Test
  public void pollingThatRunsOutOfBudgetReturnsTheLastStatusRatherThanFailing() {
    UUID id = UUID.randomUUID();
    route("POST /v1/index", 200, indexJson(id, "PENDING", 0));
    route("GET /v1/index/" + id, 200, indexJson(id, "PROCESSING", 0));

    IndexResponse response =
        facade(Duration.ofMillis(20), Duration.ofMillis(1))
            .index("hikaru", "chess.com", "2026-06", "2026-06", false, false);

    assertThat(response.status()).isEqualTo("PROCESSING");
    assertThat(response.id()).isEqualTo(id);
  }

  /** Already finished on submit — a cached month — must not be polled at all. */
  @Test
  public void aRequestThatIsAlreadyCompleteIsNotPolled() {
    UUID id = UUID.randomUUID();
    route("POST /v1/index", 200, indexJson(id, "COMPLETED", 10));

    IndexResponse response =
        facade().index("hikaru", "chess.com", "2026-06", "2026-06", false, false);

    assertThat(response.status()).isEqualTo("COMPLETED");
    assertThat(requestLog).containsExactly("POST /v1/index");
  }

  // ---- failure paths ----------------------------------------------------------------------

  /**
   * A 4xx is the caller's problem and arrives as IllegalArgumentException carrying one_d4's own
   * message — which is what every tool already catches, so a rejected ChessQL query reads to an MCP
   * client exactly as it did when the compiler ran in this process.
   */
  @Test
  public void anUpstreamBadRequestBecomesAnArgumentErrorCarryingItsMessage() {
    route("POST /v1/query", 400, "{\"error\":\"Unknown field: nonsense\",\"position\":7}");

    assertThatThrownBy(() -> facade().query("nonsense = 1", null, 10))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unknown field: nonsense");
  }

  /** one_d4's REST field names are translated to the tool's argument names on the way out. */
  @Test
  public void upstreamFieldNamesAreTranslatedToToolArgumentNames() {
    route(
        "POST /v1/index", 400, "{\"error\":\"player is required and startMonth must be YYYY-MM\"}");

    assertThatThrownBy(() -> facade().index("", "chess.com", "bad", "2026-06", false, false))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("username is required")
        .hasMessageContaining("start_month must be YYYY-MM")
        .as("a client cannot act on a field name that is not on the tool")
        .hasMessageNotContaining("startMonth");
  }

  @Test
  public void anUpstreamServerErrorIsReportedAsAnUpstreamFailure() {
    route("POST /v1/aggregate", 500, "{\"error\":\"Internal server error\"}");

    assertThatThrownBy(() -> facade().aggregate("white.elo > 1", List.of("eco"), null, 20))
        .isInstanceOf(OneD4Client.UpstreamException.class)
        .hasMessageContaining("500");
  }

  /** A body this client cannot parse is an upstream failure, not a silently empty result. */
  @Test
  public void anUnreadableResponseIsAnUpstreamFailure() {
    route("POST /v1/query", 200, "<html>gateway</html>");

    assertThatThrownBy(() -> facade().query("motif(pin)", null, 10))
        .isInstanceOf(OneD4Client.UpstreamException.class)
        .hasMessageContaining("unreadable");
  }

  /** Nothing listening at all: the message names the address so the misconfiguration is visible. */
  @Test
  public void anUnreachableUpstreamIsReportedWithItsAddress() {
    OneD4Client dead =
        new OneD4Client(
            new Jdk11HttpClient(java.net.http.HttpClient.newHttpClient()),
            JsonUtils.mapper(),
            "http://localhost:1");
    IndexerFacade facade =
        new IndexerFacade(dead, Duration.ofSeconds(1), Duration.ofMillis(1), m -> {});

    assertThatThrownBy(() -> facade.query("motif(pin)", null, 10))
        .isInstanceOf(OneD4Client.UpstreamException.class)
        .hasMessageContaining("localhost:1");
  }

  /** A base URL with a trailing slash must not produce //v1/query. */
  @Test
  public void aTrailingSlashOnTheBaseUrlIsNormalized() {
    route("POST /v1/query", 200, "{\"games\":[],\"count\":0}");
    OneD4Client client =
        new OneD4Client(
            new Jdk11HttpClient(java.net.http.HttpClient.newHttpClient()),
            JsonUtils.mapper(),
            baseUrl + "/");

    new IndexerFacade(client).query("motif(pin)", null, 10);

    assertThat(requestLog).containsExactly("POST /v1/query");
  }
}
