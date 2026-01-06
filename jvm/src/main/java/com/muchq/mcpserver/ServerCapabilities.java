package com.muchq.mcpserver;

import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.Map;

public record ServerCapabilities(@JsonProperty("tools") ToolsCapability tools) {}
