package com.muchq.mcpserver;

import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.List;

public record ToolsListResult(@JsonProperty("tools") List<Tool> tools) {}
