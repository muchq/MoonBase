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
        .as("a client that asks for a revision the server speaks is answered at that revision")
        .isEqualTo("2025-06-18");
    assertThat(result.at("/capabilities/tools").isMissingNode())
        .as("a server that advertises no tools capability is one no client will call tools on")
        .isFalse();
    assertThat(result.at("/serverInfo/name").asText()).isEqualTo("1d4-mcp");
    assertThat(result.at("/serverInfo/version").asText())
        .as(
            "MCP_SERVER_VERSION's documented default, and the canary for application.yml:"
                + " serverInfo is the only thing a client sees that comes from the file, so an"
                + " empty one here is how a config load that silently did not happen surfaces")
        .isEqualTo("1.0.0");
  }

  /**
   * Two caveats have to appear in both search tools' descriptions — that a never-indexed period
   * comes back empty rather than as an error, and that opening families are not a normalized
   * taxonomy. A description is all a model has when it decides whether to call, so a caveat missing
   * from one of them is a wrong answer the caller cannot detect.
   *
   * <p>They are shared constants rather than two copies, and a constant reaches the wire only if
   * the annotation processor evaluates the concatenation, so this asserts the served text.
   */
  @Test
  public void theSharedCorpusCaveatsReachBothSearchToolDescriptions() throws Exception {
    JsonNode tools = toolsList(17);

    for (String name : List.of("query_chess_games", "aggregate_chess_games")) {
      String description = toolNamed(tools, name).get("description").asText();
      assertThat(description)
          .as("%s must say an unindexed period reads as an empty result", name)
          .contains("played no games then");
      assertThat(description)
          .as("%s must say opening families are not normalized", name)
          .contains("Closed Sicilian Defense");
    }
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
   * <p>The types are the library's to emit and nothing in this package writes a schema, so a
   * version bump is the only thing that can put an invalid one on the wire. Asserted against the
   * served result for that reason: it holds whatever the library does internally.
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
   * around: {@code "number"} is a real JSON Schema type, so no validator rejects it, and the cost
   * is only that a model may offer 10.5 for a limit — which fails binding with a JSON-RPC error
   * rather than silently truncating.
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
   * A schema derived from a method signature carries types and descriptions and nothing else: no
   * {@code enum} on time_class/color, no {@code items} or {@code maxItems} on the array arguments.
   * The tools still reject bad values at runtime, so this costs guidance to the model rather than
   * correctness — {@code toolArgumentsAreBoundToTheDeclaredTypes} covers the enforcement half.
   * Pinned because re-growing constraints is the kind of thing that should be noticed rather than
   * assumed.
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
   * A JSON-RPC error does not always ride on HTTP 200: micronaut-mcp maps the protocol codes onto
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
   * own input still answers with a normal result — an {@code {"error": ...}} payload — rather than
   * failing the call.
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

  /**
   * What the server will negotiate up to, which is not what any single request reveals: ask for a
   * revision it speaks and it answers at that revision, so an assertion on a version the request
   * itself named only proves the echo. Asking for one it does not speak is what surfaces the
   * ceiling. Today that is 2025-11-25, from mcp-core; the spec's current revision is 2026-07-28 and
   * no Java SDK implements it yet, so this fails the day the ceiling moves — which is the day to
   * revisit the transport docs.
   */
  @Test
  public void anUnsupportedProtocolVersionIsAnsweredWithTheServersCeiling() throws Exception {
    HttpResponse<String> response =
        post(
            """
            {"jsonrpc":"2.0","id":11,"method":"initialize","params":{
              "protocolVersion":"2026-07-28","capabilities":{},
              "clientInfo":{"name":"protocol-test","version":"1.0.0"}}}
            """);

    assertThat(response.statusCode()).isEqualTo(200);
    assertThat(json(response).at("/result/protocolVersion").asText()).isEqualTo("2025-11-25");
  }

  /**
   * The wire names are the published contract and nothing else pins them. The tool tests call the
   * methods with typed arguments, which leaves their keys decoupled from {@code @ToolArg(name =
   * ...)} — so swapping two of those, include_pgn for include_tcn say, changes what every client
   * must send and breaks no other test. Exact key sets, so an added or renamed argument has to come
   * through here.
   */
  @Test
  public void eachToolAdvertisesExactlyItsDocumentedArguments() throws Exception {
    JsonNode tools = toolsList(12);

    assertThat(propertyNames(tools, "index_chess_games"))
        .containsExactlyInAnyOrder(
            "username", "platform", "start_month", "end_month", "exclude_bullet", "skip_cache");
    assertThat(propertyNames(tools, "chess_com_games"))
        .containsExactlyInAnyOrder(
            "username",
            "year",
            "month",
            "time_class",
            "color",
            "rated",
            "rules",
            "opponent",
            "include_pgn",
            "include_tcn",
            "limit",
            "offset");
    assertThat(propertyNames(tools, "aggregate_chess_games"))
        .containsExactlyInAnyOrder("query", "group_by", "player", "limit");
    assertThat(propertyNames(tools, "query_chess_games"))
        .containsExactlyInAnyOrder("query", "player", "limit", "include_pgn");
    assertThat(propertyNames(tools, "index_status")).containsExactly("request_id");
    assertThat(propertyNames(tools, "chess_com_players")).containsExactly("usernames");
    assertThat(propertyNames(tools, "analyze_position")).containsExactly("pgn");
    assertThat(propertyNames(tools, "server_time")).isEmpty();
  }

  /**
   * A required argument the caller omits is the framework's to reject, before the method runs, so
   * no tool carries a "username is required" branch for it. It comes back as a tool result flagged
   * {@code isError}, which is MCP's channel for "the call failed", not as a JSON-RPC error.
   */
  @Test
  public void aMissingRequiredArgumentIsReportedAsAToolError() throws Exception {
    HttpResponse<String> response =
        post(
            """
            {"jsonrpc":"2.0","id":13,"method":"tools/call",
             "params":{"name":"chess_com_player","arguments":{}}}
            """);

    JsonNode result = json(response).get("result");
    assertThat(result.get("isError").asBoolean())
        .as("an unbindable call must not read as a successful tool result")
        .isTrue();
    assertThat(result.at("/content/0/text").asText()).contains("username");
  }

  /**
   * Two error channels, and the tools use the quieter one. The framework flags its own failures
   * with {@code isError} — the test above — while a tool that rejects its own arguments returns a
   * successful result whose text happens to be {@code {"error": ...}}, so a model has to
   * string-match the payload to learn the call failed.
   *
   * <p>Pinned rather than fixed here: making the tools set {@code isError} means returning {@code
   * CallToolResult} from all ten, which drags mcp-core's types into a package that currently knows
   * nothing about the protocol. That is a trade worth making deliberately, not as a footnote to
   * this migration — so this test records today's answer and fails if it drifts.
   */
  @Test
  public void aToolsOwnValidationErrorIsNotFlaggedAsAnMcpToolError() throws Exception {
    HttpResponse<String> response =
        post(
            """
            {"jsonrpc":"2.0","id":16,"method":"tools/call",
             "params":{"name":"chess_com_games",
                       "arguments":{"username":"x","year":"2025","month":"13"}}}
            """);

    JsonNode result = json(response).get("result");
    assertThat(result.get("isError").asBoolean())
        .as("known gap: tool-level rejections do not use MCP's isError channel")
        .isFalse();
    assertThat(result.at("/content/0/text").asText()).contains("invalid month");
  }

  /**
   * A list argument whose elements are not strings must not become a 500. The derived schema says
   * only "array" — it carries no {@code items} type — so a model has nothing telling it to send
   * strings, and the tool has to cope.
   */
  @Test
  public void aNonStringElementInAListArgumentIsHandledNotFatal() throws Exception {
    HttpResponse<String> response =
        post(
            """
            {"jsonrpc":"2.0","id":14,"method":"tools/call",
             "params":{"name":"aggregate_chess_games",
                       "arguments":{"query":"white.elo > 1","group_by":["opening_family",5]}}}
            """);

    assertThat(response.statusCode())
        .as("a numeric group_by element is a binding the tool has to absorb, not a 500")
        .isEqualTo(200);
    assertThat(json(response).at("/result/content/0/text").asText())
        .as("the compiler rejects the unknown field by name, which is a usable message")
        .contains("5");
  }

  /**
   * The sibling of {@code aNonStringElementInAListArgumentIsHandledNotFatal}, for the other tool
   * that takes a list. Both signatures are {@code List<?>} for the same reason and only one of them
   * was covered, which made the second a claim rather than a guarantee.
   *
   * <p>Fifty-one numeric usernames rather than two: the loop that stringifies and dedupes elements
   * runs before the size check, so an oversized batch still reaches it — the exact place a declared
   * {@code List<String>} throws ClassCastException — and then returns the size error without a
   * single call going out to chess.com. That is what keeps this test off the network.
   */
  @Test
  public void aNonStringElementInTheUsernamesArrayIsHandledNotFatal() throws Exception {
    List<String> numbers = new java.util.ArrayList<>();
    for (int i = 1; i <= 51; i++) {
      numbers.add(Integer.toString(i));
    }
    HttpResponse<String> response =
        post(
            "{\"jsonrpc\":\"2.0\",\"id\":18,\"method\":\"tools/call\","
                + "\"params\":{\"name\":\"chess_com_players\",\"arguments\":{\"usernames\":["
                + String.join(",", numbers)
                + "]}}}");

    assertThat(response.statusCode()).as("a numeric usernames element must not 500").isEqualTo(200);
    JsonNode result = json(response).get("result");
    assertThat(result.at("/isError").asBoolean(false)).as("nor may it fail the call").isFalse();
    assertThat(result.at("/content/0/text").asText())
        .as("reaching the size error proves every element was stringified by the loop first")
        .contains("too many usernames: 51");
  }

  /**
   * The spec requires clients to offer both media types on POST. The reference client sends this
   * header on every request, so a suite that never does is validating a shape no real client uses.
   */
  @Test
  public void theSpecMandatedAcceptHeaderIsHonoured() throws Exception {
    HttpRequest request =
        HttpRequest.newBuilder()
            .uri(URI.create(endpoint))
            .header("Content-Type", "application/json")
            .header("Accept", "application/json, text/event-stream")
            .POST(
                HttpRequest.BodyPublishers.ofString(
                    "{\"jsonrpc\":\"2.0\",\"id\":15,\"method\":\"tools/list\"}"))
            .build();
    HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());

    assertThat(response.statusCode()).isEqualTo(200);
    assertThat(JsonUtils.mapper().readTree(response.body()).at("/result/tools")).isNotEmpty();
  }

  private static java.util.List<String> propertyNames(JsonNode tools, String toolName)
      throws Exception {
    JsonNode properties = toolNamed(tools, toolName).at("/inputSchema/properties");
    assertThat(properties.isObject()).as("%s has no inputSchema.properties", toolName).isTrue();
    java.util.List<String> names = new java.util.ArrayList<>();
    properties.fieldNames().forEachRemaining(names::add);
    return names;
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
