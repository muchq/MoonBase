package com.muchq.chatserver;

import com.fasterxml.jackson.annotation.JsonProperty;

public record ClientInfo(@JsonProperty("name") String name, @JsonProperty("version") String version) {}
