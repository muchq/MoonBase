package com.muchq.games.one_d4.e2e;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.platform.json.JsonUtils;
import io.micronaut.context.ApplicationContext;
import io.micronaut.runtime.server.EmbeddedServer;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

/**
 * #1302 at the caller's boundary: raw JSON to a real server over real HTTP, against a corpus that
 * contains an untitled opponent.
 *
 * <p>The compiler test pins the SQL and the DAO test pins the rows, and both would have kept
 * passing if {@code /v1/query} never reached that compiler — a validator that rejected {@code NOT},
 * a cached first page, a response mapper that dropped the row. This is the level the bug was
 * actually reported from: someone asked for a player's games against non-GM opponents and got a
 * smaller number than the truth, with no error to go on.
 *
 * <p>"Untitled" and "unknown" are one value here, because the column stores one value for both:
 * {@code TitleRoster::TitleOf} answers "" for a player holding no title and for a roster it could
 * not read, and the worker's upsert writes {@code NULLIF($8, '')} either way. A negated title
 * filter therefore admits both, which is the honest reading of a NULL — the alternative would be to
 * claim a distinction the schema does not carry.
 */
public class NegatedFilterE2ETest {

  private static final String PLAYER = "hikaru";

  private static EmbeddedServer server;
  private static HttpClient http;

  @BeforeAll
  public static void startServer() {
    server =
        ApplicationContext.run(
            EmbeddedServer.class,
            java.util.Map.of(
                "indexer.db.url",
                "jdbc:h2:mem:negated_filter_e2e_" + System.nanoTime() + ";DB_CLOSE_DELAY=-1",
                "micronaut.server.port",
                "-1"));
    http = HttpClient.newHttpClient();

    UUID requestId =
        server
            .getApplicationContext()
            .getBean(IndexingRequestStore.class)
            .createOrAdopt(
                PLAYER,
                "CHESS_COM",
                "2024-01",
                "2024-01",
                false,
                false,
                Duration.ofHours(1),
                Instant.now())
            .request()
            .id();

    // Four games from hikaru's side, one per opponent title, including the untitled opponent the
    // negated filter used to drop. Both colors, so the perspective CASE is exercised in both
    // directions rather than only where the player sat white.
    server
        .getApplicationContext()
        .getBean(GameFeatureStore.class)
        .insertBatch(
            List.of(
                game(requestId, "gm-white", PLAYER, "gmfoe", "IM", "GM"),
                game(requestId, "gm-black", "gmfoe2", PLAYER, "GM", "IM"),
                game(requestId, "fm-white", PLAYER, "fmfoe", "IM", "FM"),
                game(requestId, "untitled-black", "untitled_foe", PLAYER, null, "IM")));
  }

  @AfterAll
  public static void stopServer() {
    server.stop();
  }

  /**
   * The reported query. Two of the four opponents are GMs, so the answer is the other two — and the
   * untitled one is the half that used to vanish.
   */
  @Test
  public void negatedTitleFilterReturnsTheUntitledOpponentOverHttp() throws Exception {
    assertThat(urlsFor("opponent.title != \\\"GM\\\""))
        .containsExactly(
            "https://chess.com/game/fm-white", "https://chess.com/game/untitled-black");

    assertThat(urlsFor("NOT opponent.title = \\\"GM\\\""))
        .as("the other spelling of the same question")
        .containsExactly(
            "https://chess.com/game/fm-white", "https://chess.com/game/untitled-black");

    assertThat(urlsFor("NOT opponent.title IN [\\\"GM\\\", \\\"FM\\\"]"))
        .containsExactly("https://chess.com/game/untitled-black");
  }

  /**
   * The control, and the reason the test above cannot pass by returning everything: the positive
   * filter still excludes the untitled opponent, and the two halves partition the corpus. That sum
   * is the check a caller could not make from outside before #1311 made the null bucket visible —
   * it read 3 of 4 and looked like an answer.
   */
  @Test
  public void positiveAndNegatedTitleFiltersPartitionTheCorpus() throws Exception {
    List<String> areGm = urlsFor("opponent.title = \\\"GM\\\"");
    List<String> areNotGm = urlsFor("opponent.title != \\\"GM\\\"");

    assertThat(areGm)
        .containsExactly("https://chess.com/game/gm-black", "https://chess.com/game/gm-white");
    assertThat(areGm).doesNotContainAnyElementsOf(areNotGm);
    assertThat(areGm.size() + areNotGm.size())
        .as("every game has exactly one opponent, so the two halves must sum to the corpus")
        .isEqualTo(4);
  }

  /**
   * The same number, reached the other way. {@code groupBy: ["opponent.title"]} has always counted
   * untitled opponents into a {@code null} group (#1311), so the grouped answer is what the filter
   * has to agree with — and disagreeing by exactly the null bucket was the shape of this bug.
   */
  @Test
  public void theNegatedFilterAgreesWithTheGroupedCount() throws Exception {
    JsonNode aggregate =
        postJson(
            "/v1/aggregate",
            "{\"query\":\"time.class = \\\"blitz\\\"\",\"groupBy\":[\"opponent.title\"],"
                + "\"limit\":50,\"player\":\""
                + PLAYER
                + "\"}");

    long notGmByGrouping = 0;
    boolean sawNullBucket = false;
    for (JsonNode group : aggregate.get("groups")) {
      JsonNode title = group.get("group").get("opponent_title");
      boolean isNull = title == null || title.isNull();
      sawNullBucket |= isNull;
      if (isNull || !"GM".equals(title.asText())) {
        notGmByGrouping += group.get("count").asLong();
      }
    }

    assertThat(sawNullBucket).as("the untitled opponent must reach a group at all").isTrue();
    assertThat(notGmByGrouping).isEqualTo(2);
    assertThat(urlsFor("opponent.title != \\\"GM\\\"")).hasSize((int) notGmByGrouping);
  }

  /**
   * Sorted, not left in response order: ORDER BY puts NULL-valued rows at opposite ends on H2 and
   * Postgres, and these games are ordered by a column the fixture sets identically.
   */
  private static List<String> urlsFor(String chessql) throws Exception {
    JsonNode response =
        postJson(
            "/v1/query",
            "{\"query\":\""
                + chessql
                + "\",\"limit\":50,\"offset\":0,\"player\":\""
                + PLAYER
                + "\"}");
    List<String> urls = new ArrayList<>();
    for (JsonNode game : response.get("games")) {
      urls.add(game.get("gameUrl").asText());
    }
    return urls.stream().sorted().toList();
  }

  private static JsonNode postJson(String path, String body) throws Exception {
    HttpResponse<String> response =
        http.send(
            HttpRequest.newBuilder()
                .uri(URI.create("http://localhost:" + server.getPort() + path))
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(body))
                .build(),
            HttpResponse.BodyHandlers.ofString());
    assertThat(response.statusCode()).as("POST %s -> %s", path, response.body()).isEqualTo(200);
    return JsonUtils.mapper().readTree(response.body());
  }

  private static GameFeature game(
      UUID requestId,
      String slug,
      String white,
      String black,
      String whiteTitle,
      String blackTitle) {
    return new GameFeature(
        UUID.randomUUID(),
        requestId,
        "https://chess.com/game/" + slug,
        "CHESS_COM",
        white,
        black,
        2800,
        2700,
        whiteTitle,
        blackTitle,
        "blitz",
        "B10",
        "Caro Kann Defense",
        "Caro Kann Defense",
        "1-0",
        Instant.parse("2024-01-15T12:00:00Z"),
        35,
        Instant.now(),
        "1. e4 c6");
  }
}
