package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.service.PositionAnalyzer;
import com.muchq.platform.json.JsonUtils;
import io.micronaut.context.ApplicationContext;
import io.micronaut.runtime.server.EmbeddedServer;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.util.Map;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

/**
 * POST /v1/analyze against a real server, over HTTP.
 *
 * <p>{@code PositionAnalyzerTest} covers the analysis and its bounds directly. What only a booted
 * server can show is the rest of it: that the route exists, that the response serializes to the
 * shape MCP clients parse, that a bad PGN comes back 400 rather than 500 — and, the one that
 * matters most here, that the analyzer the deployment wires uses the same {@link FeatureExtractor}
 * bean the indexer does. A unit test can only prove the analyzer works when handed the right
 * detectors; it cannot prove the module hands it those.
 */
public class AnalyzeEndpointTest {

  private static final String PGN =
      """
      [Event "Live Chess"]
      [Result "1-0"]

      1. e4 e5 2. f4 d6 3. Nf3 Nc6 4. Bb5 Bd7 5. Nc3 f6 6. f5 Be7 7. Nh4 h5 \
      8. Ng6 Rh6 9. Nd5 Nd4 10. Bxd7+ Qxd7 11. d3 Rh7 12. h4 c6 13. Ngxe7 Nxe7 \
      14. Nxe7 Kxe7 15. Be3 c5 16. g4 hxg4 17. Qxg4 Qa4 18. Bxd4 cxd4 19. Qg6 Rah8 \
      20. a3 Qxc2 21. O-O Rxh4 22. Qxg7+ Ke8 23. Qg6+ Kf8 24. Qxf6+ Ke8 25. Qe6+ Kd8 \
      26. Qxd6+ Kc8 27. Qe6+ Kb8 28. Qxe5+ Ka8 29. Rf2 Rh1+ 30. Kg2 R8h2+ 31. Qxh2 Rxh2+ \
      32. Kxh2 Qxf2+ 33. Kh1 Qxb2 34. Rg1 a6 35. f6 Qf2 36. e5 Qf3+ 37. Kh2 Qf4+ \
      38. Rg3 Qxe5 39. f7 Qh5+ 40. Kg2 Qxf7 41. Rf3 Qa2+ 42. Kg3 Qxa3 43. Kf4 Qf8+ \
      44. Ke4 Qe8+ 45. Kxd4 Qd7+ 46. Ke5 a5 47. d4 a4 48. d5 Qg7+ 49. Ke6 Qg4+ \
      50. Rf5 a3 51. d6 Kb8 52. d7 Qg7 53. d8=Q+ Ka7 54. Ra5# 1-0
      """;

  private static EmbeddedServer server;
  private static HttpClient client;
  private static String baseUrl;

  @BeforeAll
  public static void startServer() {
    server =
        ApplicationContext.run(
            EmbeddedServer.class,
            Map.of(
                "indexer.db.url",
                "jdbc:h2:mem:analyze_test_" + System.nanoTime() + ";DB_CLOSE_DELAY=-1",
                "micronaut.server.port",
                "-1"));
    client = HttpClient.newHttpClient();
    baseUrl = "http://localhost:" + server.getPort();
  }

  @AfterAll
  public static void stopServer() {
    server.stop();
  }

  @Test
  public void analyzeReturnsMoveCountMotifsAndOccurrences() throws Exception {
    HttpResponse<String> response = post(json(PGN));

    assertThat(response.statusCode()).isEqualTo(200);
    JsonNode body = JsonUtils.mapper().readTree(response.body());
    assertThat(body.get("numMoves").asInt()).isEqualTo(54);
    assertThat(body.get("motifs").isArray()).isTrue();
    assertThat(body.get("motifs")).isNotEmpty();
    assertThat(body.get("occurrences").isObject()).isTrue();

    // The shape analyze_position has always put in front of a model: every advertised motif is a
    // key, and every occurrence carries the per-move detail.
    for (JsonNode motif : body.get("motifs")) {
      JsonNode occurrences = body.get("occurrences").get(motif.asText());
      assertThat(occurrences)
          .as("%s is advertised but has no occurrences", motif.asText())
          .isNotNull();
      assertThat(occurrences).isNotEmpty();
      JsonNode first = occurrences.get(0);
      assertThat(first.has("ply")).isTrue();
      assertThat(first.has("moveNumber")).isTrue();
      assertThat(first.has("side")).isTrue();
      assertThat(first.has("description")).isTrue();
    }
  }

  /**
   * The parity claim, which is the reason this endpoint exists at all: the analyzer the module
   * builds must run the same extractor the indexer runs. Comparing the served motif set against the
   * container's own {@link FeatureExtractor} bean makes a swapped detector list fail here rather
   * than as a query that mysteriously cannot find a game analysis said had a fork.
   */
  @Test
  public void theServedMotifsMatchTheExtractorTheIndexerUses() throws Exception {
    JsonNode body = JsonUtils.mapper().readTree(post(json(PGN)).body());

    FeatureExtractor indexingExtractor =
        server.getApplicationContext().getBean(FeatureExtractor.class);
    GameFeatures direct = indexingExtractor.extract(PGN);

    assertThat(body.get("numMoves").asInt()).isEqualTo(direct.numMoves());

    // Every motif the endpoint reports, other than the ones derived from the ATTACK primitive,
    // has to be one the shared extractor detected for itself.
    for (JsonNode motif : body.get("motifs")) {
      Motif parsed = Motif.valueOf(motif.asText().toUpperCase(java.util.Locale.ROOT));
      boolean derived =
          parsed == Motif.FORK
              || parsed == Motif.DISCOVERED_ATTACK
              || parsed == Motif.DISCOVERED_CHECK
              || parsed == Motif.CHECKMATE
              || parsed == Motif.DOUBLE_CHECK;
      if (!derived) {
        assertThat(direct.occurrences())
            .as("%s is not what the indexer's extractor found", parsed)
            .containsKey(parsed);
      }
    }
  }

  @Test
  public void aBlankPgnIsRejectedAsABadRequest() throws Exception {
    HttpResponse<String> response = post("{\"pgn\":\"   \"}");

    assertThat(response.statusCode()).isEqualTo(400);
    assertThat(JsonUtils.mapper().readTree(response.body()).get("error").asText())
        .contains("pgn is required");
  }

  @Test
  public void aMissingPgnFieldIsRejectedAsABadRequest() throws Exception {
    HttpResponse<String> response = post("{}");

    assertThat(response.statusCode()).isEqualTo(400);
  }

  /**
   * Oversized input is refused rather than parsed. 400 and not 413 because the limit is the
   * analyzer's rule about what is worth replaying, not the server's about what it will read.
   */
  @Test
  public void anOversizedPgnIsRejected() throws Exception {
    String huge = "1. e4 e5 ".repeat(PositionAnalyzer.MAX_PGN_BYTES / 4);

    HttpResponse<String> response = post(json(huge));

    assertThat(response.statusCode()).isEqualTo(400);
    assertThat(JsonUtils.mapper().readTree(response.body()).get("error").asText())
        .contains("too large");
  }

  /** Nothing is persisted, so analysis must not create an indexing request as a side effect. */
  @Test
  public void analyzingDoesNotWriteToTheCorpus() throws Exception {
    post(json(PGN));

    HttpResponse<String> requests =
        client.send(
            HttpRequest.newBuilder().uri(URI.create(baseUrl + "/v1/index")).GET().build(),
            HttpResponse.BodyHandlers.ofString());

    assertThat(requests.statusCode()).isEqualTo(200);
    assertThat(JsonUtils.mapper().readTree(requests.body()))
        .as("analysis indexed something")
        .isEmpty();
  }

  private static String json(String pgn) throws Exception {
    return JsonUtils.mapper().writeValueAsString(Map.of("pgn", pgn));
  }

  private static HttpResponse<String> post(String body) throws Exception {
    return client.send(
        HttpRequest.newBuilder()
            .uri(URI.create(baseUrl + "/v1/analyze"))
            .header("Content-Type", "application/json")
            .POST(HttpRequest.BodyPublishers.ofString(body))
            .build(),
        HttpResponse.BodyHandlers.ofString());
  }
}
