package com.muchq.games.chess_com_client;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonProperty;
import java.time.Instant;

public record InstantRating(int rating, Instant instant) {
  @JsonCreator
  public static InstantRating create(
      @JsonProperty("rating") int rating, @JsonProperty("date") int epochSeconds) {
    return new InstantRating(rating, Instant.ofEpochSecond(epochSeconds));
  }
}
