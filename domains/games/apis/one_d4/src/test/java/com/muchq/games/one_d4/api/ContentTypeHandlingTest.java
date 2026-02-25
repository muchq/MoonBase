package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;

import io.micronaut.context.ApplicationContext;
import io.micronaut.runtime.server.EmbeddedServer;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.util.Map;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

public class ContentTypeHandlingTest {

  private EmbeddedServer server;
  private HttpClient client;
  private String baseUrl;

  @Before
  public void setUp() {
    server =
        ApplicationContext.run(
            EmbeddedServer.class,
            Map.of(
                "indexer.db.url",
                "jdbc:h2:mem:content_type_test_" + System.nanoTime() + ";DB_CLOSE_DELAY=-1",
                "micronaut.server.port",
                "-1"));
    client = HttpClient.newHttpClient();
    baseUrl = "http://localhost:" + server.getPort();
  }

  @After
  public void tearDown() {
    server.stop();
  }

  // curl -d '{"query":"..."}' without -H 'Content-Type: application/json' sends
  // application/x-www-form-urlencoded by default, which should return 415 not 500.
  @Test
  public void query_withFormEncodedContentType_returns415() throws Exception {
    HttpRequest request =
        HttpRequest.newBuilder()
            .uri(URI.create(baseUrl + "/v1/query"))
            .header("Content-Type", "application/x-www-form-urlencoded")
            .POST(
                HttpRequest.BodyPublishers.ofString(
                    "{\"query\":\"motif(pin)\",\"limit\":10,\"offset\":0}"))
            .build();

    HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());

    assertThat(response.statusCode()).isEqualTo(415);
  }

  @Test
  public void query_withWrongContentType_returns415() throws Exception {
    HttpRequest request =
        HttpRequest.newBuilder()
            .uri(URI.create(baseUrl + "/v1/query"))
            .header("Content-Type", "text/plain")
            .POST(
                HttpRequest.BodyPublishers.ofString(
                    "{\"query\":\"motif(pin)\",\"limit\":10,\"offset\":0}"))
            .build();

    HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());

    assertThat(response.statusCode()).isEqualTo(415);
  }
}
