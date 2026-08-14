package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;

import io.micronaut.context.ApplicationContext;
import io.micronaut.runtime.server.EmbeddedServer;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.util.Map;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * Asserts the raw bytes a real server puts on the wire for /v1/index.
 *
 * <p>DtoJsonCompatTest serializes through the container's ObjectMapper bean, which is the right
 * mapper — but "the bean the HTTP layer uses" is an inference until something reads an actual
 * response body. This test is that check, and it is deliberately small: one request, one assertion
 * about which keys are present. If Micronaut ever serializes responses through something other than
 * that bean, the two tests disagree and this one is right.
 */
public class IndexResponseWireTest {

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
                "jdbc:h2:mem:wire_test_" + System.nanoTime() + ";DB_CLOSE_DELAY=-1",
                "micronaut.server.port",
                "-1"));
    client = HttpClient.newHttpClient();
    baseUrl = "http://localhost:" + server.getPort();
  }

  @AfterEach
  public void tearDown() {
    server.stop();
  }

  @Test
  public void createIndex_omitsNullFieldsRatherThanSendingThemAsNull() throws Exception {
    HttpResponse<String> response =
        client.send(
            HttpRequest.newBuilder()
                .uri(URI.create(baseUrl + "/v1/index"))
                .header("Content-Type", "application/json")
                .POST(
                    HttpRequest.BodyPublishers.ofString(
                        "{\"player\":\"wiretest\",\"platform\":\"CHESS_COM\","
                            + "\"startMonth\":\"2024-01\",\"endMonth\":\"2024-01\"}"))
                .build(),
            HttpResponse.BodyHandlers.ofString());

    assertThat(response.statusCode()).isEqualTo(200);
    String body = response.body();

    // Present, so the assertions below are about omission and not about a failed request.
    assertThat(body).contains("\"status\":\"PENDING\"").contains("\"player\":\"wiretest\"");

    // A fresh request has produced no data and hit no error. Neither key is on the wire at all —
    // clients see "absent", never "null", which is what the web UI's optional `data?` assumes and
    // what API.md documents.
    assertThat(body).doesNotContain("\"data\"").doesNotContain("\"errorMessage\"");
  }

  /**
   * The positive twin for the assertion above, sharing its fixture. Without it every assertion in
   * this class would still pass if {@code IndexResponse.data} were deleted from the record outright
   * — an absence test alone cannot tell "correctly omitted" from "never existed".
   */
  @Test
  public void completedRequest_carriesDataOnTheWire() throws Exception {
    String created =
        post(
            "{\"player\":\"wiretest\",\"platform\":\"CHESS_COM\","
                + "\"startMonth\":\"2024-01\",\"endMonth\":\"2024-01\"}");
    String id = created.replaceAll("^\\{\"id\":\"([^\"]+)\".*$", "$1");

    String body = pollUntilCompleted(id);

    // FakeChessClient defaults to an empty archive (HTTP 200), so the month indexes as empty —
    // and an empty month still records a period, which is what keeps this AVAILABLE rather than
    // EXPIRED.
    assertThat(body).contains("\"status\":\"COMPLETED\"");
    assertThat(body).contains("\"data\":");
    assertThat(body).contains("\"status\":\"AVAILABLE\"");
    assertThat(body).contains("\"monthsAvailable\":1").contains("\"monthsTotal\":1");
    assertThat(body).contains("\"expiresAt\":");
  }

  private String post(String json) throws Exception {
    HttpResponse<String> response =
        client.send(
            HttpRequest.newBuilder()
                .uri(URI.create(baseUrl + "/v1/index"))
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(json))
                .build(),
            HttpResponse.BodyHandlers.ofString());
    assertThat(response.statusCode()).isEqualTo(200);
    return response.body();
  }

  /** The worker runs in the background, so the terminal state has to be waited for. */
  private String pollUntilCompleted(String id) throws Exception {
    for (int attempt = 0; attempt < 100; attempt++) {
      HttpResponse<String> response =
          client.send(
              HttpRequest.newBuilder().uri(URI.create(baseUrl + "/v1/index/" + id)).build(),
              HttpResponse.BodyHandlers.ofString());
      assertThat(response.statusCode()).isEqualTo(200);
      if (response.body().contains("\"status\":\"COMPLETED\"")) {
        return response.body();
      }
      Thread.sleep(50);
    }
    throw new AssertionError("request " + id + " never reached COMPLETED");
  }
}
