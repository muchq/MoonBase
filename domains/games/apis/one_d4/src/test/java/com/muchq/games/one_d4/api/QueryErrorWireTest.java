package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
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

  private static final ObjectMapper MAPPER = new ObjectMapper();

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
    String escaped = chessql.replace("\\", "\\\\").replace("\"", "\\\"");
    return postBody("{\"query\":\"" + escaped + "\",\"limit\":10,\"offset\":0}");
  }

  private HttpResponse<String> postBody(String body) throws Exception {
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

  /**
   * The compiler's rejections are IllegalArgumentException, a different ErrorHandler branch from
   * ParseException — this pins that a bad played.at value is a 400 with the guidance, not the
   * generic 500, and that (unlike a ParseException) the envelope carries no {@code position} key.
   */
  @Test
  public void badPlayedAtValue_returns400WithGuidance() throws Exception {
    HttpResponse<String> response = postQuery("played.at >= \"2026-07-01\"");

    assertThat(response.statusCode()).isEqualTo(400);
    JsonNode envelope = MAPPER.readTree(response.body());
    assertThat(envelope.get("error").asText())
        .contains("played.at requires a full ISO timestamp")
        .contains("date or month");
    assertThat(envelope.get("position")).isNull();
  }

  /**
   * Error messages echo query fragments back; a fragment containing a double quote must survive
   * JSON serialization as a parseable envelope, not break it.
   */
  @Test
  public void echoedQuoteSurvivesTheJsonEnvelope() throws Exception {
    HttpResponse<String> response = postQuery("eco = 'B\"90'");

    assertThat(response.statusCode()).isEqualTo(400);
    JsonNode envelope = MAPPER.readTree(response.body());
    assertThat(envelope.get("error").asText()).contains("double quotes").contains("B\"90");
  }

  /**
   * A body that is not JSON at all is the caller's mistake, and comes back in the same envelope a
   * bad query does — not a 500, which pages for every client typo (#1472). The message is fixed
   * rather than the parser's: byte offsets and "REDACTED" source markers are not something a caller
   * can act on.
   */
  @Test
  public void malformedJson_returns400WithTheErrorEnvelope() throws Exception {
    HttpResponse<String> response = postBody("{\"query\":");

    assertThat(response.statusCode()).isEqualTo(400);
    assertThat(MAPPER.readTree(response.body()).get("error").asText())
        .isEqualTo("Request body is not valid JSON");
  }

  /** Valid JSON of the wrong shape is the same class of mistake, and the same 400. */
  @Test
  public void aWronglyTypedField_returns400WithTheErrorEnvelope() throws Exception {
    HttpResponse<String> response = postBody("{\"query\":\"motif(pin)\",\"limit\":\"ten\"}");

    assertThat(response.statusCode()).isEqualTo(400);
    assertThat(MAPPER.readTree(response.body()).get("error").asText())
        .isEqualTo("Request body does not match the request shape");
  }
}
