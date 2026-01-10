package com.muchq.mcpserver;

import com.fasterxml.jackson.annotation.JsonProperty;

public record JsonRpcError(
    @JsonProperty("code") int code, @JsonProperty("message") String message) {}
