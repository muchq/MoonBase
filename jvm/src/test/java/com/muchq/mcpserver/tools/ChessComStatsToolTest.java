package com.muchq.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.chess_com_api.ChessClient;
import com.muchq.chess_com_api.StatsResponse;
import com.muchq.json.JsonUtils;
import java.util.Map;
import java.util.Optional;
import org.junit.Test;

public class ChessComStatsToolTest {

    private static class StubChessClient extends ChessClient {
        private final Optional<StatsResponse> response;

        public StubChessClient(Optional<StatsResponse> response) {
            super(null, null);
            this.response = response;
        }

        @Override
        public Optional<StatsResponse> fetchStats(String player) {
            return response;
        }
    }

    private final StatsResponse emptyStatsResponse = new StatsResponse(
        null,
        null,
        null,
        null,
        0,
        null
    );
    private final ChessClient stubClient = new StubChessClient(Optional.of(emptyStatsResponse));
    private final ChessComStatsTool tool = new ChessComStatsTool(stubClient, JsonUtils.mapper());

    @Test
    public void testGetName() {
        assertThat(tool.getName()).isEqualTo("chess_com_stats");
    }

    @Test
    public void testGetDescription() {
        assertThat(tool.getDescription())
            .contains("chess.com")
            .contains("stats");
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
    }

    @Test
    public void testExecuteWithValidParameters() {
        Map<String, Object> arguments = Map.of("username", "hikaru");
        String result = tool.execute(arguments);
        assertThat(result).isNotNull();
    }

    @Test
    public void testExecuteWithDifferentUsername() {
        Map<String, Object> arguments = Map.of("username", "magnus");
        String result = tool.execute(arguments);
        assertThat(result).isNotNull();
    }

    @Test
    public void testExecuteWithPlayerNotFound() {
        ChessClient notFoundClient = new StubChessClient(Optional.empty());
        ChessComStatsTool notFoundTool = new ChessComStatsTool(notFoundClient, JsonUtils.mapper());

        Map<String, Object> arguments = Map.of("username", "nonexistent");
        String result = notFoundTool.execute(arguments);
        assertThat(result).isEqualTo("player not found");
    }
}
