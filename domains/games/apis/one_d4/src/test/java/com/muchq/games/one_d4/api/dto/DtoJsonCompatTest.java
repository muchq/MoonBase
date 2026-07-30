package com.muchq.games.one_d4.api.dto;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.List;
import java.util.Map;
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
}
