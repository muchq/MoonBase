package com.muchq.games.mcpserver;

import static org.assertj.core.api.Assertions.assertThat;

import ch.qos.logback.classic.Level;
import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import io.micronaut.context.ApplicationContext;
import io.micronaut.runtime.server.EmbeddedServer;
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

/**
 * A tool call leaves a server-side line: name, duration, outcome. Asserted through the real server
 * rather than against the helper, because the helper working proves nothing about the ten call
 * sites — a tool whose body never routes through it logs nothing and every other test stays green.
 *
 * <p>Both calls here resolve without the network: server_time has nothing to reach, and
 * chess_com_games rejects month 13 before building a request.
 */
public class McpToolCallLoggingTest {

  /** The helper is package-private to tools, so the logger is addressed by name. */
  private static final String TOOL_CALL_LOGGER = "com.muchq.games.mcpserver.tools.ToolCallLog";

  private static EmbeddedServer server;
  private static HttpClient client;
  private static String endpoint;

  private Logger logger;
  private ListAppender<ILoggingEvent> appender;

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
    assertThat(event.getFormattedMessage())
        .contains("tool=server_time")
        .contains("ms=")
        .contains("outcome=ok");
  }

  @Test
  public void aToolsOwnRejectionLogsAWarnLineNamingTheTool() throws Exception {
    post(
        """
        {"jsonrpc":"2.0","id":2,"method":"tools/call",
         "params":{"name":"chess_com_games",
                   "arguments":{"username":"x","year":"2025","month":"13"}}}
        """);

    assertThat(appender.list).hasSize(1);
    ILoggingEvent event = appender.list.get(0);
    assertThat(event.getLevel()).isEqualTo(Level.WARN);
    assertThat(event.getFormattedMessage())
        .contains("tool=chess_com_games")
        .contains("outcome=error");
  }

  private static void post(String body) throws Exception {
    HttpRequest request =
        HttpRequest.newBuilder()
            .uri(URI.create(endpoint))
            .header("Content-Type", "application/json")
            .POST(HttpRequest.BodyPublishers.ofString(body))
            .build();
    HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());
    assertThat(response.statusCode()).isEqualTo(200);
  }
}
