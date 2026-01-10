package com.muchq.chess_com_api;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonProperty;

import java.time.Instant;
import java.util.List;

public record Player(int playerId,
                     String playerApiUrl,
                     String playerPageUrl,
                     String name,
                     String username,
                     int followers,
                     String countryUrl,
                     Instant lastOnlineAt,
                     Instant joinedAt,
                     String status,
                     boolean streamer,
                     boolean verified,
                     String league,
                     List<StreamingPlatform> streamingPlatforms) {
    @JsonCreator
    public static Player create(@JsonProperty("player_id") int playerId,
                                @JsonProperty("@id") String playerApiUrl,
                                @JsonProperty("url") String playerPageUrl,
                                @JsonProperty("name") String name,
                                @JsonProperty("username") String username,
                                @JsonProperty("followers") int followers,
                                @JsonProperty("country") String countryUrl,
                                @JsonProperty("last_online") int lastOnlineEpochSeconds,
                                @JsonProperty("joined") int joinedEpochSeconds,
                                @JsonProperty("status") String status,
                                @JsonProperty("is_streamer") boolean streamer,
                                @JsonProperty("verified") boolean verified,
                                @JsonProperty("league") String league,
                                @JsonProperty("streaming_platforms") List<StreamingPlatform> streamingPlatforms
                                ) {
        return new Player(
                playerId,
                playerApiUrl,
                playerPageUrl,
                name,
                username,
                followers,
                countryUrl,
                Instant.ofEpochSecond(lastOnlineEpochSeconds),
                Instant.ofEpochSecond(joinedEpochSeconds),
                status,
                streamer,
                verified,
                league,
                streamingPlatforms
        );
    }
}
