package com.muchq.chess_com_api;

import com.fasterxml.jackson.annotation.JsonProperty;

public record PlayerResult(@JsonProperty("rating") int rating,
                           @JsonProperty("result") String result,
                           @JsonProperty("@id") String playerUrl,
                           @JsonProperty("username") String username,
                           @JsonProperty("uuid") String uuid
                           ) {}
