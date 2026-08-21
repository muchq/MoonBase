package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;

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

/**
 * The query error envelope at the wire: a bad ChessQL query must come back 400 with {@code
 * {"error": <message>, "position": <n>}} — raw bytes off a real server, because 1d4_web's api.ts
 * parses exactly this shape to show the message (and the message itself is the UI a human or MCP
 * client reads, so it must be the human-readable one, not a leaked token dump).
 *
 * <p>The query under test is a real user's, verbatim: {@code played.at = NULL} used to come back as
 * {@code Expected value, got: Token(IDENTIFIER, NULL, pos=12) at position 12}.
 */
public class QueryErrorWireTest {

  private EmbeddedServer server;
  private HttpClient client;
  private String baseUrl;

  @BeforeEach
  public void setUp() {
    server =
        ApplicationContext.run(
            EmbeddedServer.class,
            Map.of(
                "indexer.db.url",
                "jdbc:h2:mem:query_error_wire_test_" + System.nanoTime() + ";DB_CLOSE_DELAY=-1",
                "micronaut.server.port",
                "-1"));
    client = HttpClient.newHttpClient();
    baseUrl = "http://localhost:" + server.getPort();
  }

  @AfterEach
  public void tearDown() {
    server.stop();
  }

  private HttpResponse<String> postQuery(String chessql) throws Exception {
    String body = "{\"query\":\"" + chessql + "\",\"limit\":10,\"offset\":0}";
    HttpRequest request =
        HttpRequest.newBuilder()
            .uri(URI.create(baseUrl + "/v1/query"))
            .header("Content-Type", "application/json")
            .POST(HttpRequest.BodyPublishers.ofString(body, StandardCharsets.UTF_8))
            .build();
    return client.send(request, HttpResponse.BodyHandlers.ofString(StandardCharsets.UTF_8));
  }

  @Test
  public void nullComparison_returns400WithHumanReadableErrorAndPosition() throws Exception {
    HttpResponse<String> response = postQuery("played.at = NULL");

    assertThat(response.statusCode()).isEqualTo(400);
    assertThat(response.body())
        .contains("\"error\":\"ChessQL has no NULL literal")
        .contains("\"position\":12")
        .doesNotContain("Token(");
  }

  /** The control: the same route answers a well-formed query, so the 400 above is the parser's. */
  @Test
  public void wellFormedQuery_returns200() throws Exception {
    HttpResponse<String> response = postQuery("motif(fork)");

    assertThat(response.statusCode()).isEqualTo(200);
    assertThat(response.body()).contains("\"count\":");
  }
}
