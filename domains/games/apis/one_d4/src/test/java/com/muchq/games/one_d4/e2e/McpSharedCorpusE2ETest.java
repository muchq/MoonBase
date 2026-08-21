package com.muchq.games.one_d4.e2e;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.muchq.games.mcpserver.tools.AggregateGamesTool;
import com.muchq.games.mcpserver.tools.IndexGamesTool;
import com.muchq.games.mcpserver.tools.IndexerFacade;
import com.muchq.games.mcpserver.tools.OneD4Client;
import com.muchq.games.mcpserver.tools.QueryGamesTool;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.platform.http_client.jdk.Jdk11HttpClient;
import com.muchq.platform.json.JsonUtils;
import io.micronaut.context.ApplicationContext;
import io.micronaut.runtime.server.EmbeddedServer;
import io.modelcontextprotocol.spec.McpSchema.CallToolResult;
import io.modelcontextprotocol.spec.McpSchema.TextContent;
import java.net.URI;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

/**
 * One corpus, reached both ways.
 *
 * <p>This is the headline claim of #1332 and the sentence now on 1d4.net/mcp: what you index
 * through the MCP tools is the same corpus the site serves. {@code IndexerFacadeHttpTest} stops at
 * a fake upstream, so both sides of the seam were tested and the seam itself was not — a base URL
 * that pointed somewhere else, a path that did not match, a response the client could not read,
 * would all have passed everything.
 *
 * <p>So: a real one_d4 with a real database, the real {@code OneD4Client} and the real tool objects
 * over real HTTP. The corpus is seeded through {@code GameFeatureStore} — the same rows a worker
 * flush writes — because indexing does not run in this JVM: the C++ one_d4_worker claims requests
 * off the table (#1389), so what this service owes an index submit is the row, not the work. The
 * submit tool's test asserts exactly that contract.
 *
 * <p>What this deliberately does not do is boot a second Micronaut context for mcpserver. Both
 * factories on one classpath would wire each other's beans, so the MCP context under test would not
 * be the one that ships. Protocol framing and tool registration are covered where they belong, in
 * {@code McpProtocolTest} and {@code McpToolRosterContractTest}; this covers everything below them.
 */
public class McpSharedCorpusE2ETest {

  private static final String GAME_URL = "https://chess.com/game/live/e2e-1";

  private static EmbeddedServer oneD4;
  private static java.net.http.HttpClient http;
  private static IndexGamesTool indexTool;
  private static QueryGamesTool queryTool;
  private static AggregateGamesTool aggregateTool;

  @BeforeAll
  public static void startOneD4() {
    oneD4 =
        ApplicationContext.run(
            EmbeddedServer.class,
            Map.of(
                "indexer.db.url",
                "jdbc:h2:mem:mcp_e2e_" + System.nanoTime() + ";DB_CLOSE_DELAY=-1",
                "micronaut.server.port",
                "-1"));
    http = java.net.http.HttpClient.newHttpClient();

    // The corpus, written the way a worker flush writes it: a request row first (game_features
    // carries a foreign key onto it), then the game. The fixture's players are White and Black,
    // White won — the aggregate metrics tests read that result back through the stack.
    UUID requestId =
        oneD4
            .getApplicationContext()
            .getBean(com.muchq.games.one_d4.db.IndexingRequestStore.class)
            .createOrAdopt(
                "seed",
                "CHESS_COM",
                "2024-01",
                "2024-01",
                false,
                false,
                java.time.Duration.ofHours(1),
                Instant.now())
            .request()
            .id();
    oneD4
        .getApplicationContext()
        .getBean(GameFeatureStore.class)
        .insertBatch(
            List.of(
                new GameFeature(
                    UUID.randomUUID(),
                    requestId,
                    GAME_URL,
                    "CHESS_COM",
                    "White",
                    "Black",
                    2800,
                    2700,
                    null,
                    null,
                    "blitz",
                    "C50",
                    "Italian Game",
                    "Italian Game",
                    "1-0",
                    Instant.parse("2024-01-15T12:00:00Z"),
                    35,
                    Instant.now(),
                    "1. e4 e5")));

    // The wiring McpModule performs, built by hand because a second Micronaut context is not
    // usable here. Same client, same mapper, same facade the deployment gets.
    OneD4Client client =
        new OneD4Client(
            new Jdk11HttpClient(java.net.http.HttpClient.newHttpClient()),
            JsonUtils.mapper(),
            "http://localhost:" + oneD4.getPort(),
            "http://localhost:" + oneD4.getPort());
    IndexerFacade facade = new IndexerFacade(client);
    indexTool = new IndexGamesTool(facade);
    queryTool = new QueryGamesTool(facade);
    aggregateTool = new AggregateGamesTool(facade);
  }

  @AfterAll
  public static void stopOneD4() {
    oneD4.stop();
  }

  /**
   * The submit contract: creating the row IS the dispatch (#1389). The tool's answer must carry a
   * request id and a live status — the C++ worker claims the row from there, and {@code
   * index_status} is how a caller follows it. A COMPLETED here would mean this JVM ran an indexer
   * again.
   *
   * <p>A two-month range, deliberately: the facade polls single-month submits toward a completion
   * that no in-process worker will ever produce, and burning its whole inline budget to receive the
   * same PENDING is IndexerFacadeHttpTest's edge to cover against a fake upstream.
   */
  @Test
  public void indexingThroughMcpCreatesTheRowTheWorkerWillClaim() throws Exception {
    String indexed =
        payloadOf(
            indexTool.indexChessGames("hikaru", "chess.com", "2024-01", "2024-02", null, null));
    JsonNode indexResult = JsonUtils.mapper().readTree(indexed);

    assertThat(indexResult.path("error").isMissingNode()).as("submit failed: %s", indexed).isTrue();
    assertThat(indexResult.get("status").asText()).isEqualTo("PENDING");
    String requestId = indexResult.get("id").asText();

    // The row is real and visible through one_d4's own API, which is what the worker's claim and
    // the index_status tool both read.
    HttpResponse<String> status =
        http.send(
            HttpRequest.newBuilder()
                .uri(URI.create("http://localhost:" + oneD4.getPort() + "/v1/index/" + requestId))
                .GET()
                .build(),
            HttpResponse.BodyHandlers.ofString());
    assertThat(status.statusCode()).isEqualTo(200);
    assertThat(JsonUtils.mapper().readTree(status.body()).get("status").asText())
        .isEqualTo("PENDING");
  }

  /**
   * The seeded corpus, read back through both doors.
   *
   * <p>The one_d4 read is what makes this a shared-corpus test rather than a round-trip: if the MCP
   * tools were reading somewhere of their own — which is exactly what they did before #1332 — the
   * MCP query would answer and the site would see something else.
   */
  @Test
  public void aGameInTheCorpusIsVisibleToOneD4AndMcpAlike() throws Exception {
    // Door one: one_d4's own API, which is what api.1d4.net and the Games page use.
    HttpResponse<String> direct =
        http.send(
            HttpRequest.newBuilder()
                .uri(URI.create("http://localhost:" + oneD4.getPort() + "/v1/query"))
                .header("Content-Type", "application/json")
                .POST(
                    HttpRequest.BodyPublishers.ofString(
                        "{\"query\":\"num.moves >= 0\",\"limit\":50,\"offset\":0}"))
                .build(),
            HttpResponse.BodyHandlers.ofString());
    assertThat(direct.statusCode()).isEqualTo(200);
    JsonNode siteGames = JsonUtils.mapper().readTree(direct.body()).get("games");
    assertThat(siteGames).as("the site sees nothing of the corpus").isNotEmpty();

    // Door two: the MCP tool, over HTTP to the same service.
    JsonNode mcpGames =
        JsonUtils.mapper()
            .readTree(payloadOf(queryTool.queryChessGames("num.moves >= 0", null, 50, null)));
    assertThat(mcpGames.get("games")).isNotEmpty();

    List<String> siteUrls = urls(siteGames);
    List<String> mcpUrls = urls(mcpGames.get("games"));
    assertThat(mcpUrls)
        .as("the same corpus has to answer the same games through either door")
        .containsExactlyInAnyOrderElementsOf(siteUrls);
    assertThat(mcpUrls).contains(GAME_URL);
  }

  /** Aggregation over the shared corpus, through the extra hop. */
  @Test
  public void aggregatingThroughMcpCountsTheSharedCorpus() throws Exception {
    JsonNode aggregate =
        JsonUtils.mapper()
            .readTree(
                payloadOf(
                    aggregateTool.aggregateChessGames(
                        "num.moves >= 0", List.of("platform"), null, 20, null, null)));

    assertThat(aggregate.path("error").isMissingNode())
        .as("aggregate failed: %s", aggregate)
        .isTrue();
    assertThat(aggregate.get("groups")).isNotEmpty();
    assertThat(aggregate.get("totalGames").asLong())
        .as("the untruncated total has to survive the hop")
        .isPositive();
  }

  /**
   * The outcome metrics through the whole stack: the tool argument, the HTTP body, one_d4's
   * compiler and database, and back through the client's deserialization into the tool's JSON.
   * Every layer between here and the SQL has its own test; this is the one that would catch a
   * metric the client dropped because it deserialized into a record component nobody added.
   */
  @Test
  public void aggregatingThroughMcpCarriesTheOutcomeMetrics() throws Exception {
    JsonNode aggregate =
        JsonUtils.mapper()
            .readTree(
                payloadOf(
                    aggregateTool.aggregateChessGames(
                        // The seeded game's players are White and Black; the perspective is
                        // White's, who won it.
                        "white.username = \"White\" OR black.username = \"White\"",
                        List.of("time_class"),
                        "White",
                        20,
                        "score",
                        1)));

    assertThat(aggregate.path("error").isMissingNode())
        .as("aggregate failed: %s", aggregate)
        .isTrue();
    JsonNode group = aggregate.get("groups").get(0);
    assertThat(group.get("count").asLong()).isEqualTo(1);
    assertThat(group.get("wins").asLong()).as("metrics reached the tool: %s", group).isEqualTo(1);
    assertThat(group.get("losses").asLong()).isZero();
    assertThat(group.get("draws").asLong()).isZero();
    assertThat(group.get("score").asDouble()).isEqualTo(1.0);
  }

  /** The same call without a player: no metrics, rather than zeroed ones. */
  @Test
  public void aggregatingThroughMcpWithoutAPlayerCarriesNoOutcomeMetrics() throws Exception {
    JsonNode aggregate =
        JsonUtils.mapper()
            .readTree(
                payloadOf(
                    aggregateTool.aggregateChessGames(
                        "num.moves >= 0", List.of("platform"), null, 20, null, null)));

    JsonNode group = aggregate.get("groups").get(0);
    assertThat(group.has("wins")).isFalse();
    assertThat(group.has("score")).isFalse();
    assertThat(group.get("count").asLong()).isPositive();
  }

  private static List<String> urls(JsonNode games) {
    return java.util.stream.StreamSupport.stream(games.spliterator(), false)
        .map(g -> g.get("gameUrl").asText())
        .toList();
  }

  /**
   * The text payload of a tool result.
   *
   * <p>The tools return MCP's {@code CallToolResult} since #1331, so a rejection can travel on the
   * {@code isError} channel rather than only as an {@code {"error": ...}} body. This suite asserts
   * on the body, so it unwraps — and asserts the flag agrees, since a call that came back flagged
   * has failed however readable its payload is.
   */
  private static String payloadOf(CallToolResult result) {
    assertThat(result.content()).hasSize(1);
    String text = ((TextContent) result.content().get(0)).text();
    assertThat(result.isError()).as("the tool call failed: %s", text).isFalse();
    return text;
  }
}
