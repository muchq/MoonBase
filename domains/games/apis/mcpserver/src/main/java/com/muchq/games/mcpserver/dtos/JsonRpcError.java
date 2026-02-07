package com.muchq.games.mcpserver.dtos;

import com.fasterxml.jackson.annotation.JsonProperty;

public record JsonRpcError(
    @JsonProperty("code") int code, @JsonProperty("message") String message) {}
