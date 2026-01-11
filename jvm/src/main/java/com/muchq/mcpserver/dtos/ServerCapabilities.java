package com.muchq.mcpserver.dtos;

import com.fasterxml.jackson.annotation.JsonProperty;

public record ServerCapabilities(@JsonProperty("tools") ToolsCapability tools) {}
