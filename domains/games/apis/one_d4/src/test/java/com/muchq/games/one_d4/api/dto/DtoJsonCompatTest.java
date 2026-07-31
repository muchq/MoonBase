package com.muchq.games.one_d4.api.dto;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.micronaut.context.ApplicationContext;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

/**
 * Wire-compatibility tests for the request and response DTOs. QueryRequest and AggregateRequest are
 * records with an extra convenience constructor alongside the canonical one — exactly the shape
 * where Jackson creator selection can silently break — and every other test constructs them in
 * Java, so these are the only tests that prove old client payloads (no {@code player}) and new ones
 * (with it) both bind off the wire. AggregateResponse is covered for the mirror-image hazard: its
 * canonical constructor grew {@code totalGames}/{@code totalGroups}/{@code truncated}, and the
 * change is only additive on the wire if the serialized names stay stable and an old-shape payload
 * still binds.
 *
 * <p>Every assertion here goes through the container's ObjectMapper rather than a hand-built one.
 * That distinction is not cosmetic: the container configures null-omission, so a locally built
 * mapper reports keys on the wire that the service never sends.
 */
public class DtoJsonCompatTest {

  /**
   * The container's own ObjectMapper — the bean micronaut-jackson-databind hands the HTTP layer to
   * serialize response bodies. A hand-built {@code new ObjectMapper()} would pin whatever Jackson's
   * defaults happen to be rather than what this service actually puts on the wire, which is the one
   * thing these tests exist to check.
   */
  private static ApplicationContext context;

  private static ObjectMapper mapper;

  @BeforeAll
  public static void startContext() {
    context = ApplicationContext.run();
    mapper = context.getBean(ObjectMapper.class);
  }

  @AfterAll
  public static void stopContext() {
    context.close();
  }

  @Test
  public void queryRequestWithoutPlayerDeserializes() throws Exception {
    QueryRequest request = mapper.readValue("{\"query\":\"motif(pin)\"}", QueryRequest.class);

    assertThat(request.query()).isEqualTo("motif(pin)");
    assertThat(request.player()).isNull();
    assertThat(request.limit()).isEqualTo(50); // compact-constructor default
    assertThat(request.offset()).isZero();
  }

  @Test
  public void queryRequestWithPlayerDeserializes() throws Exception {
    QueryRequest request =
        mapper.readValue(
            "{\"query\":\"outcome = \\\"win\\\"\",\"player\":\"hikaru\",\"limit\":25}",
            QueryRequest.class);

    assertThat(request.query()).isEqualTo("outcome = \"win\"");
    assertThat(request.player()).isEqualTo("hikaru");
    assertThat(request.limit()).isEqualTo(25);
  }

  @Test
  public void aggregateRequestWithoutPlayerDeserializes() throws Exception {
    AggregateRequest request =
        mapper.readValue(
            "{\"query\":\"white.elo > 2000\",\"groupBy\":[\"opening_family\"]}",
            AggregateRequest.class);

    assertThat(request.query()).isEqualTo("white.elo > 2000");
    assertThat(request.groupBy()).isEqualTo(List.of("opening_family"));
    assertThat(request.player()).isNull();
    assertThat(request.limit()).isEqualTo(50);
  }

  @Test
  public void aggregateRequestWithPlayerDeserializes() throws Exception {
    AggregateRequest request =
        mapper.readValue(
            "{\"query\":\"outcome = \\\"win\\\"\",\"groupBy\":[\"opening_family\"],"
                + "\"orderBy\":\"count\",\"limit\":20,\"player\":\"hikaru\"}",
            AggregateRequest.class);

    assertThat(request.player()).isEqualTo("hikaru");
    assertThat(request.orderBy()).isEqualTo("count");
    assertThat(request.limit()).isEqualTo(20);
  }

  @Test
  public void aggregateResponseSerializesDocumentedFieldNames() throws Exception {
    AggregateResponse response =
        new AggregateResponse(
            List.of(
                new AggregateRow(Map.of("opening_family", "Caro Kann Defense"), 42),
                new AggregateRow(Map.of("opening_family", "Sicilian Defense"), 17)),
            2,
            59,
            2,
            false);

    // The exact document API.md publishes as the /v1/aggregate 200 body. Renaming any key here
    // (e.g. "count" -> "returnedGroups") is a breaking change for every existing client.
    assertThat(mapper.readTree(mapper.writeValueAsString(response)))
        .isEqualTo(
            mapper.readTree(
                "{\"groups\":["
                    + "{\"group\":{\"opening_family\":\"Caro Kann Defense\"},\"count\":42},"
                    + "{\"group\":{\"opening_family\":\"Sicilian Defense\"},\"count\":17}],"
                    + "\"count\":2,\"totalGames\":59,\"totalGroups\":2,\"truncated\":false}"));
  }

  @Test
  public void aggregateResponseWithoutTotalsStillBinds() throws Exception {
    // A body produced before totalGames/totalGroups/truncated existed: the three new record
    // components must fall back to their primitive defaults rather than failing creator binding.
    AggregateResponse response =
        mapper.readValue(
            "{\"groups\":[{\"group\":{\"opening_family\":\"Caro Kann Defense\"},\"count\":42}],"
                + "\"count\":1}",
            AggregateResponse.class);

    assertThat(response.count()).isEqualTo(1);
    assertThat(response.groups()).hasSize(1);
    assertThat(response.groups().get(0).group())
        .containsEntry("opening_family", "Caro Kann Defense");
    assertThat(response.groups().get(0).count()).isEqualTo(42);
    assertThat(response.totalGames()).isZero();
    assertThat(response.totalGroups()).isZero();
    assertThat(response.truncated()).isFalse();
  }

  @Test
  public void aggregateResponseWithTotalsRoundTrips() throws Exception {
    AggregateResponse original =
        new AggregateResponse(
            List.of(new AggregateRow(Map.of("me_color", "black", "outcome", "loss"), 103)),
            1,
            104,
            2,
            true);

    AggregateResponse roundTripped =
        mapper.readValue(mapper.writeValueAsString(original), AggregateResponse.class);

    assertThat(roundTripped).isEqualTo(original);
  }

  @Test
  public void indexResponseSerializesDataAvailabilityUnderDocumentedNames() throws Exception {
    IndexResponse response =
        new IndexResponse(
                UUID.fromString("a1b2c3d4-e5f6-7890-abcd-ef1234567890"),
                "hikaru",
                "CHESS_COM",
                "2026-06",
                "2026-07",
                "COMPLETED",
                325,
                null,
                true)
            .withData(new DataAvailability("PARTIAL", 1, 2, Instant.parse("2026-08-01T00:00:00Z")));

    // The exact document API.md publishes for a COMPLETED request. Note there is no
    // "errorMessage" key: the container's mapper omits nulls, so a healthy request simply
    // does not carry the field. expiresAt is epoch seconds with nanos, matching playedAt.
    assertThat(mapper.readTree(mapper.writeValueAsString(response)))
        .isEqualTo(
            mapper.readTree(
                "{\"id\":\"a1b2c3d4-e5f6-7890-abcd-ef1234567890\",\"player\":\"hikaru\","
                    + "\"platform\":\"CHESS_COM\",\"startMonth\":\"2026-06\","
                    + "\"endMonth\":\"2026-07\",\"status\":\"COMPLETED\",\"gamesIndexed\":325,"
                    + "\"excludeBullet\":true,"
                    + "\"data\":{\"status\":\"PARTIAL\",\"monthsAvailable\":1,"
                    + "\"monthsTotal\":2,\"expiresAt\":1785542400.000000000}}"));
  }

  /**
   * The mapper omits null fields, so a client cannot distinguish "no availability" by reading a
   * null — the key is simply absent. Both the web UI's optional `data?` field and API.md depend on
   * that being the shape, and it is the whole reason a PENDING request is safe to leave unresolved.
   */
  @Test
  public void indexResponseOmitsDataEntirelyWhenThereIsNone() throws Exception {
    IndexResponse pending =
        new IndexResponse(
            UUID.fromString("a1b2c3d4-e5f6-7890-abcd-ef1234567890"),
            "hikaru",
            "CHESS_COM",
            "2026-06",
            "2026-06",
            "PENDING",
            0,
            null,
            false);

    JsonNode json = mapper.readTree(mapper.writeValueAsString(pending));

    assertThat(json.has("data")).isFalse();
    assertThat(json.has("errorMessage")).isFalse();
    assertThat(json.get("status").asText()).isEqualTo("PENDING");
  }

  @Test
  public void indexResponseKeepsAnErrorMessageWhenThereIsOne() throws Exception {
    IndexResponse failed =
        new IndexResponse(
            UUID.randomUUID(),
            "hikaru",
            "CHESS_COM",
            "2026-06",
            "2026-06",
            "FAILED",
            0,
            "chess.com returned 429",
            false);

    // Guards the test above from passing because nulls are dropped for the wrong reason.
    assertThat(mapper.readTree(mapper.writeValueAsString(failed)).get("errorMessage").asText())
        .isEqualTo("chess.com returned 429");
  }

  @Test
  public void indexResponseWithoutDataStillBinds() throws Exception {
    // A body produced before `data` existed, and the shape still sent for a PENDING request.
    IndexResponse response =
        mapper.readValue(
            "{\"id\":\"a1b2c3d4-e5f6-7890-abcd-ef1234567890\",\"player\":\"hikaru\","
                + "\"platform\":\"CHESS_COM\",\"startMonth\":\"2026-06\",\"endMonth\":\"2026-06\","
                + "\"status\":\"PENDING\",\"gamesIndexed\":0,\"errorMessage\":null,"
                + "\"excludeBullet\":false}",
            IndexResponse.class);

    assertThat(response.status()).isEqualTo("PENDING");
    assertThat(response.gamesIndexed()).isZero();
    assertThat(response.data()).isNull();
  }

  @Test
  public void indexResponseWithDataRoundTrips() throws Exception {
    IndexResponse original =
        new IndexResponse(
                UUID.randomUUID(),
                "drawlya",
                "CHESS_COM",
                "2026-07",
                "2026-07",
                "COMPLETED",
                130,
                null,
                false)
            .withData(new DataAvailability("EXPIRED", 0, 1, null));

    assertThat(mapper.readValue(mapper.writeValueAsString(original), IndexResponse.class))
        .isEqualTo(original);
  }
}
