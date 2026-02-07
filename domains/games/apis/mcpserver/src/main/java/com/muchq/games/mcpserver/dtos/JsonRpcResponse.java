package com.muchq.games.mcpserver.dtos;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;

@JsonInclude(JsonInclude.Include.NON_NULL)
public record JsonRpcResponse(
    @JsonProperty("jsonrpc") String jsonrpc,
    @JsonProperty("id") Object id,
    @JsonProperty("result") Object result,
    @JsonProperty("error") JsonRpcError error) {}
