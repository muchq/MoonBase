package com.muchq.indexer.worker;

import org.junit.Test;

import static org.assertj.core.api.Assertions.assertThat;

public class ResultMapperTest {

    // === White wins ===

    @Test
    public void whiteWins_explicitWin() {
        assertThat(ResultMapper.mapResult("win", "resigned")).isEqualTo("1-0");
        assertThat(ResultMapper.mapResult("win", "checkmated")).isEqualTo("1-0");
        assertThat(ResultMapper.mapResult("win", "timeout")).isEqualTo("1-0");
    }

    @Test
    public void whiteWins_blackResigned() {
        assertThat(ResultMapper.mapResult(null, "resigned")).isEqualTo("1-0");
    }

    @Test
    public void whiteWins_blackCheckmated() {
        assertThat(ResultMapper.mapResult(null, "checkmated")).isEqualTo("1-0");
    }

    @Test
    public void whiteWins_blackTimeout() {
        assertThat(ResultMapper.mapResult(null, "timeout")).isEqualTo("1-0");
    }

    @Test
    public void whiteWins_blackAbandoned() {
        assertThat(ResultMapper.mapResult(null, "abandoned")).isEqualTo("1-0");
    }

    @Test
    public void whiteWins_blackLose() {
        assertThat(ResultMapper.mapResult(null, "lose")).isEqualTo("1-0");
    }

    // === Black wins ===

    @Test
    public void blackWins_explicitWin() {
        assertThat(ResultMapper.mapResult("resigned", "win")).isEqualTo("0-1");
        assertThat(ResultMapper.mapResult("checkmated", "win")).isEqualTo("0-1");
        assertThat(ResultMapper.mapResult("timeout", "win")).isEqualTo("0-1");
    }

    @Test
    public void blackWins_whiteResigned() {
        assertThat(ResultMapper.mapResult("resigned", null)).isEqualTo("0-1");
    }

    @Test
    public void blackWins_whiteCheckmated() {
        assertThat(ResultMapper.mapResult("checkmated", null)).isEqualTo("0-1");
    }

    @Test
    public void blackWins_whiteTimeout() {
        assertThat(ResultMapper.mapResult("timeout", null)).isEqualTo("0-1");
    }

    @Test
    public void blackWins_whiteAbandoned() {
        assertThat(ResultMapper.mapResult("abandoned", null)).isEqualTo("0-1");
    }

    @Test
    public void blackWins_whiteLose() {
        assertThat(ResultMapper.mapResult("lose", null)).isEqualTo("0-1");
    }

    // === Draws ===

    @Test
    public void draw_agreed() {
        assertThat(ResultMapper.mapResult("agreed", "agreed")).isEqualTo("1/2-1/2");
        assertThat(ResultMapper.mapResult("agreed", null)).isEqualTo("1/2-1/2");
        assertThat(ResultMapper.mapResult(null, "agreed")).isEqualTo("1/2-1/2");
    }

    @Test
    public void draw_repetition() {
        assertThat(ResultMapper.mapResult("repetition", "repetition")).isEqualTo("1/2-1/2");
        assertThat(ResultMapper.mapResult("repetition", null)).isEqualTo("1/2-1/2");
    }

    @Test
    public void draw_stalemate() {
        assertThat(ResultMapper.mapResult("stalemate", "stalemate")).isEqualTo("1/2-1/2");
    }

    @Test
    public void draw_insufficient() {
        assertThat(ResultMapper.mapResult("insufficient", "insufficient")).isEqualTo("1/2-1/2");
    }

    @Test
    public void draw_50move() {
        assertThat(ResultMapper.mapResult("50move", "50move")).isEqualTo("1/2-1/2");
    }

    @Test
    public void draw_timevsinsufficient() {
        assertThat(ResultMapper.mapResult("timevsinsufficient", "timevsinsufficient")).isEqualTo("1/2-1/2");
    }

    @Test
    public void draw_drawn() {
        assertThat(ResultMapper.mapResult("drawn", null)).isEqualTo("1/2-1/2");
    }

    // === Unknown ===

    @Test
    public void unknown_bothNull() {
        assertThat(ResultMapper.mapResult(null, null)).isEqualTo("unknown");
    }

    @Test
    public void unknown_unrecognizedValues() {
        assertThat(ResultMapper.mapResult("something", "else")).isEqualTo("unknown");
    }

    // === isDrawResult ===

    @Test
    public void isDrawResult_recognizesAllDrawTypes() {
        assertThat(ResultMapper.isDrawResult("agreed")).isTrue();
        assertThat(ResultMapper.isDrawResult("repetition")).isTrue();
        assertThat(ResultMapper.isDrawResult("stalemate")).isTrue();
        assertThat(ResultMapper.isDrawResult("insufficient")).isTrue();
        assertThat(ResultMapper.isDrawResult("50move")).isTrue();
        assertThat(ResultMapper.isDrawResult("timevsinsufficient")).isTrue();
        assertThat(ResultMapper.isDrawResult("drawn")).isTrue();
    }

    @Test
    public void isDrawResult_rejectsNonDraws() {
        assertThat(ResultMapper.isDrawResult("win")).isFalse();
        assertThat(ResultMapper.isDrawResult("resigned")).isFalse();
        assertThat(ResultMapper.isDrawResult("checkmated")).isFalse();
        assertThat(ResultMapper.isDrawResult(null)).isFalse();
    }

    // === isLossResult ===

    @Test
    public void isLossResult_recognizesAllLossTypes() {
        assertThat(ResultMapper.isLossResult("resigned")).isTrue();
        assertThat(ResultMapper.isLossResult("checkmated")).isTrue();
        assertThat(ResultMapper.isLossResult("timeout")).isTrue();
        assertThat(ResultMapper.isLossResult("abandoned")).isTrue();
        assertThat(ResultMapper.isLossResult("lose")).isTrue();
    }

    @Test
    public void isLossResult_rejectsNonLosses() {
        assertThat(ResultMapper.isLossResult("win")).isFalse();
        assertThat(ResultMapper.isLossResult("agreed")).isFalse();
        assertThat(ResultMapper.isLossResult("repetition")).isFalse();
        assertThat(ResultMapper.isLossResult(null)).isFalse();
    }

    // === Edge cases ===

    @Test
    public void explicitWinTakesPrecedenceOverLoss() {
        // If both sides have a result, "win" should be authoritative
        assertThat(ResultMapper.mapResult("win", "resigned")).isEqualTo("1-0");
        assertThat(ResultMapper.mapResult("resigned", "win")).isEqualTo("0-1");
    }

    @Test
    public void drawTakesPrecedenceOverUnknown() {
        // A draw result on either side should yield 1/2-1/2
        assertThat(ResultMapper.mapResult("repetition", "unknown_value")).isEqualTo("1/2-1/2");
        assertThat(ResultMapper.mapResult("unknown_value", "stalemate")).isEqualTo("1/2-1/2");
    }
}
