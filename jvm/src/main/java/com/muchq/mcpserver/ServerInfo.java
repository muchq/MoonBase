package com.muchq.mcpserver;

import com.fasterxml.jackson.annotation.JsonProperty;

public record ServerInfo(
    @JsonProperty("name") String name, @JsonProperty("version") String version) {}
