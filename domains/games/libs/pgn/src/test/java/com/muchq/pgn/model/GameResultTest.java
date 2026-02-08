package com.muchq.pgn.model;

import org.junit.Test;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

public class GameResultTest {

    @Test
    public void fromNotation_whiteWins() {
        assertThat(GameResult.fromNotation("1-0")).isEqualTo(GameResult.WHITE_WINS);
    }

    @Test
    public void fromNotation_blackWins() {
        assertThat(GameResult.fromNotation("0-1")).isEqualTo(GameResult.BLACK_WINS);
    }

    @Test
    public void fromNotation_draw() {
        assertThat(GameResult.fromNotation("1/2-1/2")).isEqualTo(GameResult.DRAW);
    }

    @Test
    public void fromNotation_ongoing() {
        assertThat(GameResult.fromNotation("*")).isEqualTo(GameResult.ONGOING);
    }

    @Test
    public void fromNotation_invalidThrows() {
        assertThatThrownBy(() -> GameResult.fromNotation("invalid"))
            .isInstanceOf(IllegalArgumentException.class);
    }

    @Test
    public void notation_roundTrip() {
        for (GameResult result : GameResult.values()) {
            assertThat(GameResult.fromNotation(result.notation())).isEqualTo(result);
        }
    }
}
