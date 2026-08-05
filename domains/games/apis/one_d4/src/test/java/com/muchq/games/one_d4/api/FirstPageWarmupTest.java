package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.db.GameFeatureStore;
import io.micronaut.context.ApplicationContext;
import io.micronaut.runtime.server.EmbeddedServer;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.jdbi.v3.core.Jdbi;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * Pins the wiring the unit tests cannot: that {@link FirstPageWarmer}'s {@code @Scheduled} method
 * actually fires in a real Micronaut context, and that the raw JSON body 1d4_web sends on first
 * load is the request the cache answers. If the annotation, the bean scope, the scheduling
 * processor dependency, or Jackson's mapping of the request body onto the cache key is lost, this
 * is the test that notices — the unit tests all construct {@code QueryRequest} in Java and call
 * {@code refresh()} by hand.
 */
public class FirstPageWarmupTest {

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
                "jdbc:h2:mem:first_page_warmup_test_" + System.nanoTime() + ";DB_CLOSE_DELAY=-1",
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
  public void cacheIsWarmedShortlyAfterStartupWithoutAnyRequest() throws Exception {
    FirstPageCache cache = awaitWarm();

    assertThat(cache.get())
        .as("the scheduled warmer populates the cache with no traffic")
        .isPresent();
    // Empty database, so the warmed snapshot is an empty page — warm and empty, not absent.
    assertThat(cache.get().orElseThrow().count()).isEqualTo(0);
  }

  /**
   * The browser's exact first-load JSON must be answered from the snapshot, not the database.
   * Proven by making the two disagree: a game inserted after the warm is invisible to the cached
   * default request but visible to the live path. The second POST is the control — without it, an
   * empty first response is indistinguishable from a broken insert or a query that matches nothing.
   */
  @Test
  public void defaultRequestJsonIsServedFromTheWarmedSnapshot() throws Exception {
    awaitWarm();
    insertOneGame("https://chess.com/game/after-warm");

    // The shared fixture, POSTed verbatim — the same file GamesView.test.tsx pins the frontend
    // default against, so this request is by construction the one the browser sends.
    String firstLoadBody;
    try (java.io.InputStream in =
        FirstPageWarmupTest.class.getResourceAsStream("/first_page_request.json")) {
      assertThat(in).as("fixture missing from test resources").isNotNull();
      firstLoadBody = new String(in.readAllBytes(), java.nio.charset.StandardCharsets.UTF_8);
    }

    HttpResponse<String> cached = postQuery(firstLoadBody);
    assertThat(cached.statusCode()).isEqualTo(200);
    assertThat(cached.body())
        .as("the default request is answered from the pre-insert snapshot")
        .contains("\"count\":0")
        .doesNotContain("after-warm");

    HttpResponse<String> live =
        postQuery("{\"query\":\"num.moves >= 0\",\"limit\":26,\"offset\":0}");
    assertThat(live.statusCode()).isEqualTo(200);
    assertThat(live.body())
        .as("control: any other page size goes to the database and sees the insert")
        .contains("\"count\":1")
        .contains("after-warm");
  }

  private FirstPageCache awaitWarm() throws Exception {
    FirstPageCache cache = server.getApplicationContext().getBean(FirstPageCache.class);
    // The warmer's initial delay is 1s; give a loaded CI machine headroom without running into
    // the small-test 60s ceiling.
    long deadline = System.nanoTime() + java.time.Duration.ofSeconds(20).toNanos();
    while (cache.get().isEmpty() && System.nanoTime() < deadline) {
      Thread.sleep(100);
    }
    return cache;
  }

  private HttpResponse<String> postQuery(String body) throws Exception {
    HttpRequest request =
        HttpRequest.newBuilder()
            .uri(URI.create(baseUrl + "/v1/query"))
            .header("Content-Type", "application/json")
            .POST(HttpRequest.BodyPublishers.ofString(body))
            .build();
    return client.send(request, HttpResponse.BodyHandlers.ofString());
  }

  private void insertOneGame(String gameUrl) {
    UUID requestId = UUID.randomUUID();
    server
        .getApplicationContext()
        .getBean(Jdbi.class)
        .useHandle(
            h ->
                h.createUpdate(
                        "INSERT INTO indexing_requests (id, player, platform, start_month,"
                            + " end_month) VALUES (:id, 'wire-test', 'CHESS_COM', '2026-01',"
                            + " '2026-01')")
                    .bind("id", requestId)
                    .execute());
    server
        .getApplicationContext()
        .getBean(GameFeatureStore.class)
        .insertBatch(
            List.of(
                new GameFeature(
                    UUID.randomUUID(),
                    requestId,
                    gameUrl,
                    "CHESS_COM",
                    "white",
                    "black",
                    2000,
                    1900,
                    null,
                    "GM",
                    "blitz",
                    "B90",
                    "Sicilian Defense Najdorf Variation",
                    "Sicilian Defense",
                    "1-0",
                    Instant.now(),
                    30,
                    Instant.now(),
                    "pgn")));
  }
}
