package com.muchq.games.chess_com_client;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonProperty;
import java.time.Instant;
import java.util.List;

public record Player(
    int playerId,
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
    List<StreamingPlatform> streamingPlatforms,
    String title,
    String location,
    Integer fideRating) {
  @JsonCreator
  public static Player create(
      @JsonProperty("player_id") int playerId,
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
      @JsonProperty("streaming_platforms") List<StreamingPlatform> streamingPlatforms,
      @JsonProperty("title") String title,
      @JsonProperty("location") String location,
      @JsonProperty("fide") Integer fideRating) {
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
        streamingPlatforms,
        // title is absent for untitled players; location and fide are optional profile fields
        title,
        location,
        fideRating);
  }
}
