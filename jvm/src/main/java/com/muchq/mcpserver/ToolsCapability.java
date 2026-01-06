package com.muchq.mcpserver;

import com.fasterxml.jackson.annotation.JsonProperty;

public record ToolsCapability(@JsonProperty("listChanged") boolean listChanged) {}
