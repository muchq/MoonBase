package com.muchq.games.mcpserver.dtos;

import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.Map;

public record Tool(
    @JsonProperty("name") String name,
    @JsonProperty("description") String description,
    @JsonProperty("inputSchema") Map<String, Object> inputSchema) {}
