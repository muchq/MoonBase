package com.muchq.games.one_d4.api.dto;

import com.fasterxml.jackson.annotation.JsonProperty;

public record IndexRequest(
    String player,
    String platform,
    String startMonth,
    String endMonth,
    @JsonProperty("includeBullet") Boolean includeBullet
) {
    public IndexRequest {
        if (includeBullet == null) {
            includeBullet = false;
        }
    }

    public IndexRequest(String player, String platform, String startMonth, String endMonth) {
        this(player, platform, startMonth, endMonth, false);
    }
}
