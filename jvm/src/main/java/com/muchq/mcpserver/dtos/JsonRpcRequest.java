package com.muchq.mcpserver.dtos;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;

@JsonInclude(JsonInclude.Include.NON_NULL)
public record JsonRpcRequest(
    @JsonProperty("jsonrpc") String jsonrpc,
    @JsonProperty("id") Object id,
    @JsonProperty("method") String method,
    @JsonProperty("params") Object params) {}
