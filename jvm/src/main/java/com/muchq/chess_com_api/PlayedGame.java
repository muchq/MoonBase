package com.muchq.chess_com_api;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonProperty;
import java.time.Instant;

public record PlayedGame(String url,
                         String pgn,
                         Instant endTime,
                         boolean rated,
                         Accuracies accuracies,
                         String tcn,
                         String uuid,
                         String initialSetup,
                         String fen,
                         String timeClass,
                         String rules,
                         PlayerResult whiteResult,
                         PlayerResult blackResult,
                         String eco
                         ) {

    @JsonCreator
    public static PlayedGame create(
            @JsonProperty("url") String url,
            @JsonProperty("pgn") String pgn,
            @JsonProperty("end_time") int endTimeEpochSeconds,
            @JsonProperty("rated") boolean rated,
            @JsonProperty("accuracies") Accuracies accuracies,
            @JsonProperty("tcn") String tcn,
            @JsonProperty("uuid") String uuid,
            @JsonProperty("initial_setup") String initialSetup,
            @JsonProperty("fen") String fen,
            @JsonProperty("time_class") String timeClass,
            @JsonProperty("rules") String rules,
            @JsonProperty("white") PlayerResult whiteResult,
            @JsonProperty("black") PlayerResult blackResult,
            @JsonProperty("eco") String eco
            ){
        return new PlayedGame(
                url,
                pgn,
                Instant.ofEpochSecond(endTimeEpochSeconds),
                rated,
                accuracies,
                tcn,
                uuid,
                initialSetup,
                fen,
                timeClass,
                rules,
                whiteResult,
                blackResult,
                eco
        );
    }
}
