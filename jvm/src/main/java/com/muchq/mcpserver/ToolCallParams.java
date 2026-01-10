package com.muchq.mcpserver;

import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.Map;

public record ToolCallParams(
    @JsonProperty("name") String name, @JsonProperty("arguments") Map<String, Object> arguments) {}
