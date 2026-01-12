package com.muchq.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.chess_com_api.ChessClient;
import com.muchq.chess_com_api.GamesResponse;
import com.muchq.json.JsonUtils;
import java.time.YearMonth;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import org.junit.Test;

public class ChessComGamesToolTest {

    private static class StubChessClient extends ChessClient {
        private final Optional<GamesResponse> response;

        public StubChessClient(Optional<GamesResponse> response) {
            super(null, null);
            this.response = response;
        }

        @Override
        public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
            return response;
        }
    }

    private final GamesResponse emptyGamesResponse = new GamesResponse(List.of());
    private final ChessClient stubClient = new StubChessClient(Optional.of(emptyGamesResponse));
    private final ChessComGamesTool tool = new ChessComGamesTool(stubClient, JsonUtils.mapper());

    @Test
    public void testGetName() {
        assertThat(tool.getName()).isEqualTo("chess_com_games");
    }

    @Test
    public void testGetDescription() {
        assertThat(tool.getDescription())
            .contains("chess.com games")
            .contains("month")
            .contains("year");
    }

    @Test
    public void testGetInputSchema() {
        Map<String, Object> schema = tool.getInputSchema();
        assertThat(schema).containsKey("type");
        assertThat(schema).containsKey("properties");
        assertThat(schema).containsKey("required");

        @SuppressWarnings("unchecked")
        Map<String, Object> properties = (Map<String, Object>) schema.get("properties");
        assertThat(properties).containsKey("username");
        assertThat(properties).containsKey("year");
        assertThat(properties).containsKey("month");
    }

    @Test
    public void testExecuteWithValidParameters() {
        Map<String, Object> arguments = Map.of(
            "username", "drawlya",
            "year", "2026",
            "month", "01"
        );
        String result = tool.execute(arguments);
        assertThat(result).isNotNull();
    }

    @Test
    public void testExecuteWithDifferentMonthFormat() {
        Map<String, Object> arguments = Map.of(
            "username", "hikaru",
            "year", "2025",
            "month", "12"
        );
        String result = tool.execute(arguments);
        assertThat(result).isNotNull();
    }

    @Test
    public void testExecuteWithSingleDigitMonth() {
        Map<String, Object> arguments = Map.of(
            "username", "magnus",
            "year", "2024",
            "month", "5"
        );
        String result = tool.execute(arguments);
        assertThat(result).isNotNull();
    }

    @Test
    public void testExecuteWithInvalidYear() {
        Map<String, Object> arguments = Map.of(
            "username", "testuser",
            "year", "invalid",
            "month", "01"
        );
        assertThatThrownBy(() -> tool.execute(arguments))
            .isInstanceOf(NumberFormatException.class);
    }

    @Test
    public void testExecuteWithInvalidMonth() {
        Map<String, Object> arguments = Map.of(
            "username", "testuser",
            "year", "2025",
            "month", "invalid"
        );
        assertThatThrownBy(() -> tool.execute(arguments))
            .isInstanceOf(NumberFormatException.class);
    }
}
