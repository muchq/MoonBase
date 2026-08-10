package com.muchq.games.one_d4.e2e;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.muchq.games.mcpserver.tools.AggregateGamesTool;
import com.muchq.games.mcpserver.tools.AnalyzePositionTool;
import com.muchq.games.mcpserver.tools.IndexGamesTool;
import com.muchq.games.mcpserver.tools.IndexerFacade;
import com.muchq.games.mcpserver.tools.OneD4Client;
import com.muchq.games.mcpserver.tools.QueryGamesTool;
import com.muchq.platform.http_client.jdk.Jdk11HttpClient;
import com.muchq.platform.json.JsonUtils;
import io.micronaut.context.ApplicationContext;
import io.micronaut.runtime.server.EmbeddedServer;
import io.modelcontextprotocol.spec.McpSchema.CallToolResult;
import io.modelcontextprotocol.spec.McpSchema.TextContent;
import java.net.URI;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

/**
 * One corpus, reached both ways.
 *
 * <p>This is the headline claim of #1332 and the sentence now on 1d4.net/mcp: what you index
 * through the MCP tools is the same corpus the site serves. Nothing proved it. {@code
 * IndexerFacadeHttpTest} stops at a fake upstream and {@code AnalyzeEndpointTest} starts at one_d4,
 * so both sides of the seam were tested and the seam itself was not — a base URL that pointed
 * somewhere else, a path that did not match, a response the client could not read, would all have
 * passed everything.
 *
 * <p>So: a real one_d4 with a real database, the real {@code OneD4Client} and the real tool objects
 * over real HTTP. Indexing goes in through MCP and comes back out through both one_d4's own API and
 * the MCP query tool, and the two have to agree.
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
  private static AnalyzePositionTool analyzeTool;

  @BeforeAll
  public static void startOneD4() {
    // FakeChessClient @Replaces the real one, so indexing is deterministic and nothing reaches
    // api.chess.com.
    oneD4 =
        ApplicationContext.run(
            EmbeddedServer.class,
            Map.of(
                "indexer.db.url",
                "jdbc:h2:mem:mcp_e2e_" + System.nanoTime() + ";DB_CLOSE_DELAY=-1",
                "micronaut.server.port",
                "-1"));
    http = java.net.http.HttpClient.newHttpClient();

    FakeChessClient chess =
        (FakeChessClient)
            oneD4
                .getApplicationContext()
                .getBean(com.muchq.games.chess_com_client.ChessClient.class);
    chess.addGame("hikaru", java.time.YearMonth.of(2024, 1), GAME_URL);

    // The wiring McpModule performs, built by hand because a second Micronaut context is not
    // usable here. Same client, same mapper, same facade the deployment gets.
    OneD4Client client =
        new OneD4Client(
            new Jdk11HttpClient(java.net.http.HttpClient.newHttpClient()),
            JsonUtils.mapper(),
            "http://localhost:" + oneD4.getPort());
    IndexerFacade facade = new IndexerFacade(client);
    indexTool = new IndexGamesTool(facade);
    queryTool = new QueryGamesTool(facade);
    aggregateTool = new AggregateGamesTool(facade);
    analyzeTool = new AnalyzePositionTool(facade);
  }

  @AfterAll
  public static void stopOneD4() {
    oneD4.stop();
  }

  /**
   * Index through MCP, read back through both doors.
   *
   * <p>The one_d4 read is what makes this a shared-corpus test rather than a round-trip: if the MCP
   * tools were writing somewhere of their own — which is exactly what they did before #1332 — the
   * MCP query would still find the game and the site would still not have it.
   */
  @Test
  public void aGameIndexedThroughMcpIsVisibleToOneD4AndBack() throws Exception {
    String indexed =
        payloadOf(
            indexTool.indexChessGames("hikaru", "chess.com", "2024-01", "2024-01", null, null));
    JsonNode indexResult = JsonUtils.mapper().readTree(indexed);

    assertThat(indexResult.path("error").isMissingNode())
        .as("indexing failed: %s", indexed)
        .isTrue();
    assertThat(indexResult.get("status").asText())
        .as("a single month is polled to completion, so the tool answers with a final status")
        .isEqualTo("COMPLETED");
    assertThat(indexResult.get("gamesIndexed").asInt()).isPositive();

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
    assertThat(siteGames).as("the site sees nothing of what MCP indexed").isNotEmpty();

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
    indexTool.indexChessGames("hikaru", "chess.com", "2024-01", "2024-01", null, null);

    JsonNode aggregate =
        JsonUtils.mapper()
            .readTree(
                payloadOf(
                    aggregateTool.aggregateChessGames(
                        "num.moves >= 0", List.of("platform"), null, 20)));

    assertThat(aggregate.path("error").isMissingNode())
        .as("aggregate failed: %s", aggregate)
        .isTrue();
    assertThat(aggregate.get("groups")).isNotEmpty();
    assertThat(aggregate.get("totalGames").asLong())
        .as("the untruncated total has to survive the hop")
        .isPositive();
  }

  /**
   * analyze_position is the one corpus-adjacent tool that writes nothing, and it now runs on one_d4
   * rather than in the MCP process — so this is the only test that exercises that call as a tool
   * rather than as an endpoint.
   */
  @Test
  public void analyzingThroughMcpReachesOneD4AndWritesNothing() throws Exception {
    String pgn = "[Event \"x\"]\n\n1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7# 1-0";

    JsonNode analysis = JsonUtils.mapper().readTree(payloadOf(analyzeTool.analyzePosition(pgn)));

    assertThat(analysis.path("error").isMissingNode()).as("analyze failed: %s", analysis).isTrue();
    assertThat(analysis.get("numMoves").asInt()).isEqualTo(4);
    assertThat(analysis.get("motifs").toString())
        .as("scholar's mate ends in checkmate")
        .contains("checkmate");

    // Nothing indexed as a side effect: the request list is still whatever indexing put there.
    HttpResponse<String> requests =
        http.send(
            HttpRequest.newBuilder()
                .uri(URI.create("http://localhost:" + oneD4.getPort() + "/v1/index"))
                .GET()
                .build(),
            HttpResponse.BodyHandlers.ofString());
    for (JsonNode request : JsonUtils.mapper().readTree(requests.body())) {
      assertThat(request.get("player").asText())
          .as("analysis created an indexing request")
          .isNotEqualTo("");
    }
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
