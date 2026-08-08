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
 * <p>The framework owns the handshake, the JSON-RPC framing and the tool methods, so the risk is
 * not that this package computes them wrongly — it is that a config change, a version bump or a
 * bean that fails to register takes the endpoint off-spec with everything still compiling. These
 * tests are the only ones that would notice (#1325).
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
    assertThat(result.get("protocolVersion").asText())
        .as("the negotiated spec revision is what a client keys its behavior off")
        .isEqualTo("2025-06-18");
    assertThat(result.at("/capabilities/tools").isMissingNode())
        .as("a server that advertises no tools capability is one no client will call tools on")
        .isFalse();
    assertThat(result.at("/serverInfo/name").asText()).isEqualTo("1d4-mcp");
  }

  /**
   * A JSON-RPC notification carries no {@code id} and must draw no response object — not a result,
   * and not an error either. A client sends this one immediately after {@code initialize}, so a
   * server that answers it at all fails the handshake at step two and never reaches {@code
   * tools/list}.
   */
  @Test
  public void theInitializedNotificationIsAcceptedAndAnsweredWithNoJsonRpcBody() throws Exception {
    HttpResponse<String> response =
        post(
            """
            {"jsonrpc":"2.0","method":"notifications/initialized"}
            """);

    assertThat(response.statusCode()).isEqualTo(202);
    assertThat(response.body())
        .as("a notification must draw no response object at all, error or otherwise")
        .isEmpty();
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
   * Every advertised property type must be one JSON Schema actually defines — a client hands {@code
   * inputSchema} straight to a model provider, and a provider that validates it rejects an unknown
   * {@code type}.
   *
   * <p>micronaut-mcp 0.0.20 wrote {@code "bool"} for booleans, which is not one of them, and this
   * repo carried a response filter to correct it. Upstream fixed the constant in 2.0.0; the
   * Micronaut 5 bump picked that up and the filter is gone. The assertion never moved, because it
   * was written against the served result rather than the workaround — which is what let the filter
   * be deleted without anyone having to re-derive whether it was still load-bearing.
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

  /**
   * Streamable HTTP makes the server->client SSE stream optional, and micronaut-mcp does not
   * implement it. The spec's answer for that is 405, and a client that probes GET needs to get it:
   * anything 2xx reads as "here is a stream" and leaves the client waiting on one that never
   * arrives. The README and the /mcp page both promise this number.
   */
  @Test
  public void getReturns405BecauseTheSseStreamIsNotImplemented() throws Exception {
    HttpResponse<String> response =
        client.send(
            HttpRequest.newBuilder().uri(URI.create(endpoint)).GET().build(),
            HttpResponse.BodyHandlers.ofString());

    assertThat(response.statusCode()).isEqualTo(405);
  }

  /**
   * Integer parameters advertise {@code "number"}, not {@code "integer"} — micronaut-mcp maps every
   * {@code Number} to the one type and has no {@code TYPE_INTEGER}. Accepted rather than worked
   * around: unlike the {@code "bool"} it once emitted, {@code "number"} is a real JSON Schema type,
   * so no validator rejects it, and the cost is only that a model may offer 10.5 for a limit —
   * which fails binding with a JSON-RPC error rather than silently truncating.
   *
   * <p>Pinned so the choice is visible. If a later micronaut-mcp distinguishes them this test
   * fails, which is the moment to decide whether the tighter type is worth having.
   */
  @Test
  public void integerArgumentsAdvertiseNumber() throws Exception {
    JsonNode tools = toolsList(8);
    JsonNode properties = toolNamed(tools, "chess_com_games").at("/inputSchema/properties");

    assertThat(properties.at("/limit/type").asText()).isEqualTo("number");
    assertThat(properties.at("/offset/type").asText()).isEqualTo("number");
  }

  /**
   * Deriving the schema from the signature loses the constraints the hand-written maps carried:
   * {@code enum} on time_class/color, {@code items}/{@code maxItems} on the array arguments. The
   * tools still reject bad values at runtime with the same messages, so this costs guidance to the
   * model rather than correctness — {@code toolArgumentsAreBoundToTheDeclaredTypes} covers the
   * enforcement half. Pinned because it is a real difference, and because re-growing constraints is
   * the kind of thing that should be noticed rather than assumed.
   */
  @Test
  public void derivedSchemasCarryNoEnumOrArrayConstraints() throws Exception {
    JsonNode tools = toolsList(9);
    JsonNode games = toolNamed(tools, "chess_com_games").at("/inputSchema/properties");

    assertThat(games.at("/time_class").has("enum")).isFalse();
    assertThat(games.at("/time_class/description").asText())
        .as("the allowed values have to reach the model somehow, so they live in the description")
        .contains("blitz");

    JsonNode usernames =
        toolNamed(tools, "chess_com_players").at("/inputSchema/properties/usernames");
    assertThat(usernames.get("type").asText()).isEqualTo("array");
    assertThat(usernames.has("items")).isFalse();
    assertThat(usernames.has("maxItems")).isFalse();
  }

  /**
   * A JSON-RPC error no longer always rides on HTTP 200: micronaut-mcp maps the protocol codes onto
   * statuses, so an unknown method is a 400. Clients that read the JSON-RPC envelope are
   * unaffected, but anything that branches on the status first sees a different shape, so the
   * mapping is pinned rather than left to the library.
   */
  @Test
  public void jsonRpcErrorsCarryAMatchingHttpStatus() throws Exception {
    HttpResponse<String> unknownMethod =
        post(
            """
            {"jsonrpc":"2.0","id":10,"method":"no/such/method"}
            """);

    assertThat(unknownMethod.statusCode()).isEqualTo(400);
    assertThat(json(unknownMethod).at("/error/code").asInt()).isEqualTo(-32601);
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

  private static JsonNode toolsList(int id) throws Exception {
    return json(post("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"method\":\"tools/list\"}"))
        .at("/result/tools");
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
