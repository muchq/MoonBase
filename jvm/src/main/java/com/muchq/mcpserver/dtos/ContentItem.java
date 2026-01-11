package com.muchq.mcpserver.dtos;

import com.fasterxml.jackson.annotation.JsonProperty;

public record ContentItem(
    @JsonProperty("type") String type, @JsonProperty("text") String text) {}
