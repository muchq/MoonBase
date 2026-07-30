package com.muchq.games.one_d4.api.dto;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.UUID;
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
 */
public class DtoJsonCompatTest {

  private final ObjectMapper mapper = new ObjectMapper();

  /** Instant needs JSR-310 registered, the same way the service's mapper has it. */
  private final ObjectMapper timeAwareMapper =
      new ObjectMapper().registerModule(new JavaTimeModule());

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

    JsonNode json = timeAwareMapper.readTree(timeAwareMapper.writeValueAsString(response));

    // Every key API.md publishes, and nothing extra: renaming one is a breaking change.
    assertThat(json.properties())
        .extracting(Map.Entry::getKey)
        .containsExactlyInAnyOrder(
            "id",
            "player",
            "platform",
            "startMonth",
            "endMonth",
            "status",
            "gamesIndexed",
            "errorMessage",
            "excludeBullet",
            "data");
    assertThat(json.get("status").asText()).isEqualTo("COMPLETED");
    assertThat(json.get("gamesIndexed").asInt()).isEqualTo(325);

    JsonNode data = json.get("data");
    assertThat(data.properties())
        .extracting(Map.Entry::getKey)
        .containsExactlyInAnyOrder("status", "monthsAvailable", "monthsTotal", "expiresAt");
    assertThat(data.get("status").asText()).isEqualTo("PARTIAL");
    assertThat(data.get("monthsAvailable").asInt()).isEqualTo(1);
    assertThat(data.get("monthsTotal").asInt()).isEqualTo(2);
    // The instant's exact encoding is the server mapper's business — playedAt already rides the
    // same setting — so assert it survives the trip rather than pinning a numeric literal here.
    assertThat(
            timeAwareMapper
                .readValue(timeAwareMapper.writeValueAsString(response), IndexResponse.class)
                .data()
                .expiresAt())
        .isEqualTo(Instant.parse("2026-08-01T00:00:00Z"));
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

    assertThat(
            timeAwareMapper.readValue(
                timeAwareMapper.writeValueAsString(original), IndexResponse.class))
        .isEqualTo(original);
  }
}
