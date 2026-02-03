package com.muchq.chess_com_api;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonProperty;
import java.time.Instant;

public record Last(int rating, Instant instant, int rd) {
  @JsonCreator
  public static Last create(
      @JsonProperty("rating") int rating,
      @JsonProperty("date") int epochSeconds,
      @JsonProperty("rd") int rd) {
    return new Last(rating, Instant.ofEpochSecond(epochSeconds), rd);
  }
}
