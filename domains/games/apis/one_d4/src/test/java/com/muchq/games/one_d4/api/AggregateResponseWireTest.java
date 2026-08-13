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
 * Asserts the raw bytes a real server puts on the wire for /v1/aggregate's NULL group keys.
 *
 * <p>A NULL group key (untitled opponent, missing rating) must reach clients as an explicit {@code
 * "opponent_title":null} — the null bucket is how callers count what {@code !=} excludes, so a key
 * the HTTP mapper silently omits is indistinguishable from a dimension that was never requested.
 * The HTTP mapper omits null <em>bean fields</em> on purpose (IndexResponseWireTest pins that), and
 * with no explicit inclusion on the group map that same default swallows null map values — and, for
 * a group whose every value is NULL, the {@code group} property itself. Serializing through {@code
 * JsonUtils} (the MCP tool path) or asserting on the Java map cannot catch this; only the
 * container-configured mapper behind a real response can.
 */
public class AggregateResponseWireTest {

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
                "jdbc:h2:mem:agg_wire_test_" + System.nanoTime() + ";DB_CLOSE_DELAY=-1",
                "micronaut.server.port",
                "-1"));
    client = HttpClient.newHttpClient();
    baseUrl = "http://localhost:" + server.getPort();

    UUID requestId = UUID.randomUUID();
    server
        .getApplicationContext()
        .getBean(Jdbi.class)
        .useHandle(
            handle ->
                handle
                    .createUpdate(
                        "INSERT INTO indexing_requests (id, player, platform, start_month,"
                            + " end_month, status) VALUES (:id, 'hikaru', 'CHESS_COM', '2026-06',"
                            + " '2026-06', 'COMPLETED')")
                    .bind("id", requestId)
                    .execute());
    // One opponent with no title and no rating (the NULL bucket), one titled and rated (the
    // non-null control on the same wire).
    server
        .getApplicationContext()
        .getBean(GameFeatureStore.class)
        .insertBatch(
            List.of(
                game(requestId, "https://chess.com/game/w1", "foe", null, null),
                game(requestId, "https://chess.com/game/w2", "titled", 2450, "FM")));
  }

  @AfterEach
  public void tearDown() {
    server.stop();
  }

  @Test
  public void aggregateResponseKeepsExplicitNullGroupKeysOnTheWire() throws Exception {
    String body =
        aggregate(
            "{\"query\":\"time.class = \\\"blitz\\\"\",\"groupBy\":"
                + "[\"opponent.elo\"],\"player\":\"hikaru\"}");

    // The NULL-elo opponent's bucket: the key must be on the wire as an explicit null, not an
    // omitted entry, and not an empty (or missing) group object.
    assertThat(body).contains("\"group\":{\"opponent_elo\":null}");
    // The control on the same response: a real bucket serializes as a JSON number, so the fix
    // cannot be "stringify everything" or "drop omission globally".
    assertThat(body).contains("\"group\":{\"opponent_elo\":2400}");
  }

  @Test
  public void aggregateResponseKeepsNullDimensionAlongsideNonNullKeysOnTheWire() throws Exception {
    String body =
        aggregate(
            "{\"query\":\"time.class = \\\"blitz\\\"\",\"groupBy\":"
                + "[\"opponent.title\",\"outcome\"],\"player\":\"hikaru\"}");

    // A multi-key group must keep its NULL dimension next to the non-null one — dropping just the
    // null entry would leave a plausible-looking group of the wrong arity.
    assertThat(body).contains("\"group\":{\"opponent_title\":null,\"outcome\":\"win\"}");
    assertThat(body).contains("\"group\":{\"opponent_title\":\"FM\",\"outcome\":\"win\"}");
  }

  /**
   * The outcome metrics on the wire, present-and-zero versus absent (#1345).
   *
   * <p>Zero is a real answer — "this player has never won in this bucket" — and it has to survive
   * the mapper that omits nulls, which is the same mapper that must drop the fields entirely when
   * there is no player to attribute a result to. The two cases produce the same Java record with
   * different components set, so only the bytes can tell them apart, and only these bytes tell a
   * client whether a missing {@code wins} means zero or means nobody asked.
   */
  @Test
  public void aggregateResponseCarriesZeroOutcomeMetricsForAPlayer() throws Exception {
    // Both games are hikaru wins as White, grouped by opponent title: the FM bucket is 1-0-0 and
    // the untitled bucket is 1-0-0 too, so every metric is present and losses/draws are zero.
    String body =
        aggregate(
            "{\"query\":\"white.username = \\\"hikaru\\\" OR black.username = \\\"hikaru\\\"\","
                + "\"groupBy\":[\"opponent.title\"],\"player\":\"hikaru\"}");

    assertThat(body).contains("\"wins\":1").contains("\"losses\":0").contains("\"draws\":0");
    // Score is W + D/2, so it reaches the wire as a JSON number that can carry a half point.
    assertThat(body).contains("\"score\":1.0");
    // The control on the same response: the count is still there and still a group count.
    assertThat(body).contains("\"count\":1");
  }

  /**
   * The negative twin, byte for byte: with no player the four fields are absent rather than zero. A
   * zeroed row would be a claim the service cannot make — nobody's results were asked for.
   */
  @Test
  public void aggregateResponseOmitsOutcomeMetricsWithoutAPlayer() throws Exception {
    String body = aggregate("{\"query\":\"num.moves >= 0\",\"groupBy\":[\"time_class\"]}");

    assertThat(body).contains("\"group\":{\"time_class\":\"blitz\"}").contains("\"count\":2");
    assertThat(body)
        .doesNotContain("\"wins\"")
        .doesNotContain("\"losses\"")
        .doesNotContain("\"draws\"")
        .doesNotContain("\"score\"");
  }

  private String aggregate(String json) throws Exception {
    HttpResponse<String> response =
        client.send(
            HttpRequest.newBuilder()
                .uri(URI.create(baseUrl + "/v1/aggregate"))
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(json))
                .build(),
            HttpResponse.BodyHandlers.ofString());
    assertThat(response.statusCode()).isEqualTo(200);
    return response.body();
  }

  private static GameFeature game(
      UUID requestId, String url, String opponent, Integer opponentElo, String opponentTitle) {
    return new GameFeature(
        null,
        requestId,
        url,
        "CHESS_COM",
        "hikaru",
        opponent,
        2800,
        opponentElo,
        "GM",
        opponentTitle,
        "blitz",
        "B10",
        "Caro Kann Some Line",
        "Caro Kann",
        "1-0",
        Instant.parse("2026-06-15T12:00:00Z"),
        20,
        Instant.now(),
        "pgn");
  }
}
