package com.muchq.chess_com_api;

import com.fasterxml.jackson.annotation.JsonCreator;import com.fasterxml.jackson.annotation.JsonProperty;

public record StreamingPlatform(String type, String channelUrl) {
    @JsonCreator
    public static StreamingPlatform create(
            @JsonProperty("type") String type,
            @JsonProperty("channel_url") String channelUrl
    ) {
        return new StreamingPlatform(type, channelUrl);
    }
}
