package com.muchq.games.mcpserver;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.muchq.platform.json.JsonUtils;
import io.micronaut.context.ApplicationContext;
import io.micronaut.runtime.server.EmbeddedServer;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

/**
 * Speaks the protocol to the real server, over HTTP, the way a client does.
 *
 * <p>Every assertion here is a claim the hand-rolled transport got wrong and shipped anyway,
 * because nothing at this level ever ran: {@code notifications/initialized} answered {@code -32601}
 * — an error object for a message that must draw no response at all — so a spec-following client
 * failed at step two of the handshake and never reached {@code tools/list} (#1325). The framework
 * owns those methods now, and these tests are what keeps that true.
 */
public class McpProtocolTest {

  private static EmbeddedServer server;
  private static HttpClient client;
  private static String endpoint;

  @BeforeAll
  public static void startServer() {
    server = ApplicationContext.run(EmbeddedServer.class, Map.of("micronaut.server.port", "-1"));
    client = HttpClient.newHttpClient();
    endpoint = "http://localhost:" + server.getPort() + "/mcp";
  }

  @AfterAll
  public static void stopServer() {
    server.stop();
  }

  @Test
  public void initializeReportsToolsAndTheServerIdentity() throws Exception {
    HttpResponse<String> response =
        post(
            """
            {"jsonrpc":"2.0","id":1,"method":"initialize","params":{
              "protocolVersion":"2025-06-18","capabilities":{},
              "clientInfo":{"name":"protocol-test","version":"1.0.0"}}}
            """);

    assertThat(response.statusCode()).isEqualTo(200);
    JsonNode result = json(response).get("result");
    assertThat(result.get("protocolVersion").asText()).isNotBlank();
    assertThat(result.at("/capabilities/tools").isMissingNode())
        .as("a server that advertises no tools capability is one no client will call tools on")
        .isFalse();
    assertThat(result.at("/serverInfo/name").asText()).isEqualTo("1d4-mcp");
  }

  /**
   * The bug that motivated the migration. A JSON-RPC notification carries no {@code id} and must
   * draw no response body; the old handler ran it through the same dispatch as a request and
   * returned {@code -32601 Method not found}. A spec-following client sends this immediately after
   * {@code initialize}, so that error ended the handshake before tools were ever listed.
   */
  @Test
  public void theInitializedNotificationIsAcceptedAndAnsweredWithNoJsonRpcBody() throws Exception {
    HttpResponse<String> response =
        post(
            """
            {"jsonrpc":"2.0","method":"notifications/initialized"}
            """);

    assertThat(response.statusCode()).isBetween(200, 299);
    assertThat(response.body())
        .as("a notification must draw no response object at all, error or otherwise")
        .doesNotContain("\"error\"")
        .doesNotContain("\"result\"");
  }

  @Test
  public void toolsListAdvertisesEveryToolWithAnObjectInputSchema() throws Exception {
    HttpResponse<String> response =
        post(
            """
            {"jsonrpc":"2.0","id":2,"method":"tools/list"}
            """);

    assertThat(response.statusCode()).isEqualTo(200);
    JsonNode tools = json(response).at("/result/tools");
    assertThat(tools.isArray()).isTrue();
    assertThat(tools).isNotEmpty();
    for (JsonNode tool : tools) {
      assertThat(tool.get("name").asText()).isNotBlank();
      assertThat(tool.get("description").asText())
          .as("%s is what an assistant reads to decide whether to call it", tool.get("name"))
          .isNotBlank();
      assertThat(tool.at("/inputSchema/type").asText()).isEqualTo("object");
    }
  }

  /**
   * The declared parameters of a {@code @Tool} method are the input schema — there is no
   * hand-written map to fall out of step with the signature. server_time takes none and
   * chess_com_games takes twelve, three of them required, so this pins both ends of that.
   */
  @Test
  public void inputSchemasComeFromTheToolMethodSignatures() throws Exception {
    JsonNode tools =
        json(post(
                """
                {"jsonrpc":"2.0","id":3,"method":"tools/list"}
                """))
            .at("/result/tools");

    JsonNode serverTime = toolNamed(tools, "server_time");
    assertThat(serverTime.at("/inputSchema/properties")).isEmpty();

    JsonNode games = toolNamed(tools, "chess_com_games");
    JsonNode properties = games.at("/inputSchema/properties");
    assertThat(properties.fieldNames())
        .toIterable()
        .contains("username", "year", "month", "time_class", "include_pgn", "limit");
    assertThat(properties.at("/limit/type")).isNotNull();
    assertThat(games.at("/inputSchema/required"))
        .as("a nullable parameter is an optional property, a plain one is required")
        .extracting(node -> node.toString())
        .asString()
        .contains("username", "year", "month")
        .doesNotContain("time_class");
  }

  /**
   * Every advertised property type must be one JSON Schema actually defines. micronaut-mcp 0.0.20
   * writes {@code "bool"} for booleans, which is not one of them, and a client hands this straight
   * to a model provider — see {@link McpBooleanSchemaTypeFilter}, which corrects it. This asserts
   * the served result rather than the filter, so it keeps passing when the filter is deleted
   * against a fixed library, and fails if the filter is deleted too early.
   */
  @Test
  public void everyAdvertisedPropertyTypeIsAJsonSchemaType() throws Exception {
    JsonNode tools =
        json(post(
                """
                {"jsonrpc":"2.0","id":7,"method":"tools/list"}
                """))
            .at("/result/tools");

    List<String> jsonSchemaTypes =
        List.of("string", "number", "integer", "boolean", "object", "array", "null");
    int booleansSeen = 0;
    for (JsonNode tool : tools) {
      JsonNode properties = tool.at("/inputSchema/properties");
      for (var entry : (Iterable<Map.Entry<String, JsonNode>>) properties::fields) {
        String type = entry.getValue().path("type").asText();
        assertThat(type)
            .as(
                "%s.%s advertises a type no JSON Schema validator knows",
                tool.get("name"), entry.getKey())
            .isIn(jsonSchemaTypes);
        if ("boolean".equals(type)) {
          booleansSeen++;
        }
      }
    }
    assertThat(booleansSeen)
        .as("the roster has boolean arguments; zero here means this test proves nothing")
        .isPositive();
  }

  @Test
  public void toolsCallRunsTheToolAndReturnsItsTextContent() throws Exception {
    HttpResponse<String> response =
        post(
            """
            {"jsonrpc":"2.0","id":4,"method":"tools/call",
             "params":{"name":"server_time","arguments":{}}}
            """);

    assertThat(response.statusCode()).isEqualTo(200);
    JsonNode result = json(response).get("result");
    assertThat(result.at("/isError").asBoolean(false)).isFalse();
    String text = result.at("/content/0/text").asText();
    assertThat(Long.parseLong(text))
        .isBetween(System.currentTimeMillis() - 60_000, System.currentTimeMillis() + 60_000);
  }

  /**
   * Arguments arrive as JSON and are bound to the method's declared types. A tool that rejects its
   * own input still answers with a normal result — the {@code {"error": ...}} payload the tools
   * have always used — rather than failing the call.
   */
  @Test
  public void toolArgumentsAreBoundToTheDeclaredTypes() throws Exception {
    HttpResponse<String> response =
        post(
            """
            {"jsonrpc":"2.0","id":5,"method":"tools/call",
             "params":{"name":"chess_com_games",
                       "arguments":{"username":"hikaru","year":"2025","month":"13"}}}
            """);

    assertThat(response.statusCode()).isEqualTo(200);
    String text = json(response).at("/result/content/0/text").asText();
    assertThat(text).contains("invalid month").contains("1-12");
  }

  @Test
  public void anUnknownToolIsAJsonRpcErrorRatherThanAServerFailure() throws Exception {
    HttpResponse<String> response =
        post(
            """
            {"jsonrpc":"2.0","id":6,"method":"tools/call",
             "params":{"name":"no_such_tool","arguments":{}}}
            """);

    JsonNode body = json(response);
    assertThat(body.has("error") || body.at("/result/isError").asBoolean(false))
        .as("the caller has to be able to tell this apart from a successful call")
        .isTrue();
  }

  private static JsonNode toolNamed(JsonNode tools, String name) throws Exception {
    for (JsonNode tool : tools) {
      if (name.equals(tool.get("name").asText())) {
        return tool;
      }
    }
    throw new AssertionError("tools/list did not advertise " + name);
  }

  private static HttpResponse<String> post(String body) throws Exception {
    HttpRequest request =
        HttpRequest.newBuilder()
            .uri(URI.create(endpoint))
            .header("Content-Type", "application/json")
            .POST(HttpRequest.BodyPublishers.ofString(body))
            .build();
    return client.send(request, HttpResponse.BodyHandlers.ofString());
  }

  private static JsonNode json(HttpResponse<String> response) throws Exception {
    return JsonUtils.mapper().readTree(response.body());
  }
}
