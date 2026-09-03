package com.muchq.games.mcpserver;

import static org.assertj.core.api.Assertions.assertThat;

import ch.qos.logback.classic.Level;
import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.fasterxml.jackson.databind.JsonNode;
import com.muchq.platform.json.JsonUtils;
import io.micronaut.context.ApplicationContext;
import io.micronaut.runtime.server.EmbeddedServer;
import java.net.ServerSocket;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.util.Map;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.slf4j.LoggerFactory;
import org.slf4j.event.KeyValuePair;

/**
 * A tool call leaves a server-side line: name, duration, outcome. Asserted through the real server
 * rather than against the helper, because the helper working proves nothing about the ten call
 * sites — a tool whose body never routes through it logs nothing and every other test stays green.
 *
 * <p>Every call here resolves without the public network: server_time has nothing to reach, most
 * tools reject an argument before building a request, and index_chess_games — whose validation
 * lives in one_d4 — is aimed at a closed local port, so its failure is an immediate connection
 * refusal reported on the isError channel.
 */
public class McpToolCallLoggingTest {

  /** The helper is package-private to tools, so the logger is addressed by name. */
  private static final String TOOL_CALL_LOGGER = "com.muchq.games.mcpserver.tools.ToolCallLog";

  /**
   * One call per advertised tool, none of which leaves the machine. Keyed by wire name;
   * everyAdvertisedToolLogsExactlyOneLine fails on a tool the roster serves that has no entry here,
   * so an eleventh tool cannot ship with its logging unpinned.
   */
  private static final Map<String, String> NETWORK_FREE_ARGUMENTS =
      Map.ofEntries(
          Map.entry("server_time", "{}"),
          // " " rather than "", and [" "] rather than []: empty strings and empty arrays are
          // stripped from the arguments somewhere before the SDK's input validation, which then
          // rejects the call for a missing required property — framework-side, before any tool
          // runs, where nothing logs on this logger. A blank-but-present value passes validation
          // and is rejected by the tool itself.
          Map.entry("chess_com_player", "{\"username\":\" \"}"),
          Map.entry("chess_com_stats", "{\"username\":\" \"}"),
          Map.entry("chess_com_games", "{\"username\":\"x\",\"year\":\"2025\",\"month\":\"13\"}"),
          Map.entry("chess_com_players", "{\"usernames\":[\" \"]}"),
          Map.entry("analyze_position", "{\"pgn\":\" \"}"),
          Map.entry("query_chess_games", "{\"query\":\" \"}"),
          Map.entry("aggregate_chess_games", "{\"query\":\" \",\"group_by\":[\"opening_family\"]}"),
          Map.entry("index_status", "{\"request_id\":\"not-a-uuid\"}"),
          Map.entry(
              "index_chess_games",
              "{\"username\":\"x\",\"platform\":\"chess.com\","
                  + "\"start_month\":\"2026-01\",\"end_month\":\"2026-01\"}"));

  private static EmbeddedServer server;
  private static HttpClient client;
  private static String endpoint;

  private Logger logger;
  private ListAppender<ILoggingEvent> appender;

  @BeforeAll
  public static void startServer() throws Exception {
    // A port that was just bound and released: connecting to it refuses immediately, which is
    // what keeps the index_chess_games call in this suite off the network and fast.
    int closedPort;
    try (ServerSocket socket = new ServerSocket(0)) {
      closedPort = socket.getLocalPort();
    }
    server =
        ApplicationContext.run(
            EmbeddedServer.class,
            Map.of(
                "micronaut.server.port", "-1",
                "one.d4.base.url", "http://127.0.0.1:" + closedPort,
                "one.d4.v2.base.url", "http://127.0.0.1:" + closedPort));
    client = HttpClient.newHttpClient();
    endpoint = "http://localhost:" + server.getPort() + "/mcp";
  }

  @AfterAll
  public static void stopServer() {
    server.stop();
  }

  @BeforeEach
  public void attachAppender() {
    logger = (Logger) LoggerFactory.getLogger(TOOL_CALL_LOGGER);
    appender = new ListAppender<>();
    appender.start();
    logger.addAppender(appender);
  }

  @AfterEach
  public void detachAppender() {
    logger.detachAppender(appender);
  }

  @Test
  public void aSuccessfulToolCallLogsOneInfoLineNamingTheTool() throws Exception {
    post(
        """
        {"jsonrpc":"2.0","id":1,"method":"tools/call",
         "params":{"name":"server_time","arguments":{}}}
        """);

    assertThat(appender.list).hasSize(1);
    ILoggingEvent event = appender.list.get(0);
    assertThat(event.getLevel()).isEqualTo(Level.INFO);
    assertThat(event.getMessage()).isEqualTo("tool_call");
    assertThat(kv(event, "tool")).isEqualTo("server_time");
    assertThat(kv(event, "outcome")).isEqualTo("ok");
    assertThat(kv(event, "ms")).isInstanceOf(Long.class);
  }

  private static Object kv(ILoggingEvent event, String key) {
    for (KeyValuePair pair : java.util.Objects.requireNonNull(event.getKeyValuePairs())) {
      if (pair.key.equals(key)) {
        return pair.value;
      }
    }
    throw new AssertionError("no key-value pair " + key + " on " + event);
  }

  @Test
  public void aToolsOwnRejectionLogsAWarnLineNamingTheToolButNotItsArguments() throws Exception {
    post(
        """
        {"jsonrpc":"2.0","id":2,"method":"tools/call",
         "params":{"name":"chess_com_games",
                   "arguments":{"username":"sentinel-username-x9","year":"2025","month":"13"}}}
        """);

    assertThat(appender.list).hasSize(1);
    ILoggingEvent event = appender.list.get(0);
    assertThat(event.getLevel()).isEqualTo(Level.WARN);
    assertThat(kv(event, "tool")).isEqualTo("chess_com_games");
    assertThat(kv(event, "outcome")).isEqualTo("error");
    assertThat(event.getFormattedMessage() + event.getKeyValuePairs())
        .as("arguments are caller data and stay out of the log — README's stated contract")
        .doesNotContain("sentinel-username-x9");
  }

  /**
   * Driven by the served roster, not a hand-kept list: every advertised tool gets one call and must
   * log exactly one line naming itself at the expected level. A tool whose body stops routing
   * through ToolCallLog — or a new tool shipped unwrapped — fails here and nowhere else, and a
   * renamed {@code @Tool} whose log literal lags behind fails the name assertion.
   */
  @Test
  public void everyAdvertisedToolLogsExactlyOneLineNamingItself() throws Exception {
    HttpResponse<String> listResponse =
        post("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/list\"}");
    JsonNode tools = JsonUtils.mapper().readTree(listResponse.body()).at("/result/tools");
    assertThat(tools).isNotEmpty();

    for (JsonNode tool : tools) {
      String name = tool.get("name").asText();
      String arguments = NETWORK_FREE_ARGUMENTS.get(name);
      assertThat(arguments)
          .as("no network-free call fixture for %s — add one so its logging is pinned", name)
          .isNotNull();

      appender.list.clear();
      post(
          "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\""
              + name
              + "\",\"arguments\":"
              + arguments
              + "}}");

      assertThat(appender.list).as("%s must log exactly one line per call", name).hasSize(1);
      ILoggingEvent event = appender.list.get(0);
      assertThat(kv(event, "tool")).isEqualTo(name);
      assertThat(kv(event, "ms")).isInstanceOf(Long.class);
      // server_time is the one tool that succeeds without the network; everything else is a
      // rejection or an unreachable-one_d4 report, both on the isError channel.
      Level expected = "server_time".equals(name) ? Level.INFO : Level.WARN;
      assertThat(event.getLevel()).as("%s", name).isEqualTo(expected);
    }
  }

  private static HttpResponse<String> post(String body) throws Exception {
    HttpRequest request =
        HttpRequest.newBuilder()
            .uri(URI.create(endpoint))
            .header("Content-Type", "application/json")
            .POST(HttpRequest.BodyPublishers.ofString(body))
            .build();
    HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());
    assertThat(response.statusCode()).isEqualTo(200);
    return response;
  }
}
