package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;

import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import io.micronaut.context.ApplicationContext;
import io.micronaut.runtime.server.EmbeddedServer;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.slf4j.LoggerFactory;
import org.slf4j.event.KeyValuePair;

/**
 * The source of a query event is read from request headers bound by the JAX-RS bridge — the one
 * part of the event a unit test on the controller cannot vouch for. Real requests through a real
 * server, with the headers a browser and mcpserver actually send.
 */
public class QueryEventWireTest {

  private EmbeddedServer server;
  private HttpClient client;
  private ListAppender<ILoggingEvent> captured;

  @BeforeEach
  public void setUp() {
    server =
        ApplicationContext.run(
            EmbeddedServer.class,
            Map.of(
                "indexer.db.url",
                "jdbc:h2:mem:query_event_wire_test_" + System.nanoTime() + ";DB_CLOSE_DELAY=-1",
                "micronaut.server.port",
                "-1",
                // A bare embedded server refuses non-localhost origins outright; the deployment
                // sits behind Caddy, which owns CORS, and passes the browser's Origin through
                // (a POST with Origin https://1d4.net answers 200 there). This test is about
                // the header reaching the controller, so the origin is allowed here.
                "micronaut.server.cors.enabled",
                "true",
                "micronaut.server.cors.configurations.ui.allowed-origins",
                "https://1d4.net"));
    client = HttpClient.newHttpClient();
    captured = new ListAppender<>();
    captured.start();
    logger().addAppender(captured);
  }

  @AfterEach
  public void tearDown() {
    logger().detachAppender(captured);
    server.stop();
  }

  private static Logger logger() {
    return (Logger) LoggerFactory.getLogger(QueryEvent.LOGGER);
  }

  private String sourceOfARequestWith(String... headers) throws Exception {
    captured.list.clear();
    HttpRequest.Builder request =
        HttpRequest.newBuilder()
            .uri(URI.create("http://localhost:" + server.getPort() + "/v1/query"))
            .header("Content-Type", "application/json")
            .POST(
                HttpRequest.BodyPublishers.ofString(
                    "{\"query\":\"num.moves >= 0\",\"limit\":10,\"offset\":0}",
                    StandardCharsets.UTF_8));
    for (int i = 0; i < headers.length; i += 2) {
      request.header(headers[i], headers[i + 1]);
    }
    HttpResponse<String> response =
        client.send(request.build(), HttpResponse.BodyHandlers.ofString(StandardCharsets.UTF_8));
    assertThat(response.statusCode()).as(response.body()).isEqualTo(200);
    assertThat(captured.list).hasSize(1);
    for (KeyValuePair pair : captured.list.get(0).getKeyValuePairs()) {
      if (pair.key.equals("source")) {
        return String.valueOf(pair.value);
      }
    }
    throw new AssertionError("no source in " + captured.list.get(0).getKeyValuePairs());
  }

  @Test
  public void theHeadersBoundByTheBridgeDecideTheSource() throws Exception {
    // A browser attaches Origin to every cross-origin call; mcpserver sends its product token.
    assertThat(sourceOfARequestWith("User-Agent", "mcpserver")).isEqualTo("mcp");
    assertThat(sourceOfARequestWith("Origin", "https://1d4.net")).isEqualTo("ui");
    assertThat(sourceOfARequestWith("User-Agent", "curl/8.6.0")).isEqualTo("api");
  }
}
