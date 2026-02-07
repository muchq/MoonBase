package com.muchq.games.chess_com_client;

import com.fasterxml.jackson.annotation.JsonProperty;

public record StatsResponse(
    @JsonProperty("chess_daily") Stats chessDaily,
    @JsonProperty("chess_rapid") Stats chessRapid,
    @JsonProperty("chess_bullet") Stats chessBullet,
    @JsonProperty("chess_blitz") Stats chessBlitz,
    @JsonProperty("fide") int fide,
    @JsonProperty("tactics") Tactics tactics) {}
