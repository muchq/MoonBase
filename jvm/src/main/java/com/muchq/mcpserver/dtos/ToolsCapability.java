package com.muchq.mcpserver.dtos;

import com.fasterxml.jackson.annotation.JsonProperty;

public record ToolsCapability(@JsonProperty("listChanged") boolean listChanged) {}
