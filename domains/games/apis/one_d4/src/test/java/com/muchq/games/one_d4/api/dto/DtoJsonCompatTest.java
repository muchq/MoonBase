package com.muchq.games.one_d4.api.dto;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.List;
import org.junit.jupiter.api.Test;

/**
 * Wire-compatibility tests for the request DTOs. QueryRequest and AggregateRequest are records with
 * an extra convenience constructor alongside the canonical one — exactly the shape where Jackson
 * creator selection can silently break — and every other test constructs them in Java, so these are
 * the only tests that prove old client payloads (no {@code player}) and new ones (with it) both
 * bind off the wire.
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
}
