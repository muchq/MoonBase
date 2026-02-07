package com.muchq.games.mcpserver.dtos;

import com.fasterxml.jackson.annotation.JsonProperty;

public record InitializeResult(
    @JsonProperty("protocolVersion") String protocolVersion,
    @JsonProperty("capabilities") ServerCapabilities capabilities,
    @JsonProperty("serverInfo") ServerInfo serverInfo) {}
