package com.muchq.games.mcpserver.dtos;

import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.Map;

public record InitializeParams(
    @JsonProperty("protocolVersion") String protocolVersion,
    @JsonProperty("capabilities") Map<String, Object> capabilities,
    @JsonProperty("clientInfo") ClientInfo clientInfo) {}
