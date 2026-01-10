package com.muchq.chess_com_api;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonProperty;

import java.time.Instant;

public record Best(int rating, Instant instant, String gameUrl) {
    @JsonCreator
    public static Best create(@JsonProperty("rating") int rating,
                              @JsonProperty("date") int epochSeconds,
                              @JsonProperty("game") String gameUrl) {
        return new Best(rating, Instant.ofEpochSecond(epochSeconds), gameUrl);
    }
}
