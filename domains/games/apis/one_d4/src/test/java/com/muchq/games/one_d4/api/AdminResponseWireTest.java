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
 * The bytes the admin endpoints put on the wire, from a real server.
 *
 * <p>Both are documented in one_d4's README with a route and a response shape, and until now
 * nothing checked either. The README said {@code /admin/reanalyze} returned {@code
 * {"gamesReanalyzed": N}}; the service has never sent that key — it sends {@code gamesProcessed}
 * and {@code gamesFailed}. Prose and code disagreed for as long as no test read the response, which
 * is the failure mode the testing bar is about. Raised by review on #1374.
 *
 * <p>Asserting on raw text rather than a deserialized DTO is the point: a round trip through the
 * record would agree with itself no matter what the field is called.
 */
public class AdminResponseWireTest {

  private EmbeddedServer server;
  private HttpClient client;

  @BeforeEach
  public void setUp() {
    server =
        ApplicationContext.run(
            EmbeddedServer.class,
            Map.of(
                "indexer.db.url",
                "jdbc:h2:mem:admin_wire_test_" + System.nanoTime() + ";DB_CLOSE_DELAY=-1",
                "micronaut.server.port",
                "-1"));
    client = HttpClient.newHttpClient();
  }

  @AfterEach
  public void tearDown() {
    server.stop();
  }

  @Test
  public void rederiveOpeningsAnswersWithTheDocumentedFieldNames() throws Exception {
    // An empty corpus is enough: the field names are the contract, and zero is a real answer.
    assertThat(post("/admin/rederive-openings"))
        .isEqualTo("{\"gamesScanned\":0,\"gamesUpdated\":0}");
  }

  @Test
  public void reanalyzeAnswersWithTheDocumentedFieldNames() throws Exception {
    // The id is random, so the shape is pinned by field name rather than by
    // full-body equality. status is the async contract's load-bearing field:
    // a client that used to read counts as "done" now has to poll for them.
    String body = post("/admin/reanalyze");
    assertThat(body).contains("\"id\":\"");
    assertThat(body).contains("\"status\":\"PENDING\"");
    assertThat(body).contains("\"gamesProcessed\":0");
    assertThat(body).contains("\"gamesFailed\":0");
  }

  @Test
  public void reanalyzeIsAnEnqueueNotARun_soASecondPostAnswersWithTheSamePass() throws Exception {
    String first = post("/admin/reanalyze");
    String again = post("/admin/reanalyze");
    assertThat(again).isEqualTo(first);
  }

  @Test
  public void reanalysisStatusIsReadableAtTheIdTheEnqueueAnswered() throws Exception {
    String body = post("/admin/reanalyze");
    String id = body.replaceAll(".*\"id\":\"([0-9a-f-]{36})\".*", "$1");

    HttpResponse<String> status =
        client.send(
            HttpRequest.newBuilder()
                .uri(URI.create("http://localhost:" + server.getPort() + "/admin/reanalyze/" + id))
                .GET()
                .build(),
            HttpResponse.BodyHandlers.ofString());
    assertThat(status.statusCode()).isEqualTo(200);
    assertThat(status.body()).isEqualTo(body);
  }

  // The controller throws NoSuchElementException and trusts the global
  // ErrorHandler to make that a 404. Trusting is an argument; this is the
  // wire-level fact, for the admin route rather than the /v1 one.
  @Test
  public void anUnknownReanalysisIdIsA404() throws Exception {
    HttpResponse<String> response =
        client.send(
            HttpRequest.newBuilder()
                .uri(
                    URI.create(
                        "http://localhost:"
                            + server.getPort()
                            + "/admin/reanalyze/00000000-0000-4000-8000-000000000042"))
                .GET()
                .build(),
            HttpResponse.BodyHandlers.ofString());
    assertThat(response.statusCode()).isEqualTo(404);
  }

  private String post(String path) throws Exception {
    HttpResponse<String> response =
        client.send(
            HttpRequest.newBuilder()
                .uri(URI.create("http://localhost:" + server.getPort() + path))
                .POST(HttpRequest.BodyPublishers.noBody())
                .build(),
            HttpResponse.BodyHandlers.ofString());
    assertThat(response.statusCode()).isEqualTo(200);
    return response.body();
  }
}
