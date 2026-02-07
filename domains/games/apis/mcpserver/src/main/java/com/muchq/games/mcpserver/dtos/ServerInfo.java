package com.muchq.games.mcpserver.dtos;

import com.fasterxml.jackson.annotation.JsonProperty;

public record ServerInfo(
    @JsonProperty("name") String name, @JsonProperty("version") String version) {}
