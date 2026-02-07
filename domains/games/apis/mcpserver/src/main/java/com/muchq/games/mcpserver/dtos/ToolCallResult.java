package com.muchq.games.mcpserver.dtos;

import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.List;

public record ToolCallResult(@JsonProperty("content") List<ContentItem> content) {}
