package com.muchq.mcpserver;

import com.fasterxml.jackson.annotation.JsonProperty;

public record ContentItem(
    @JsonProperty("type") String type, @JsonProperty("text") String text) {}
