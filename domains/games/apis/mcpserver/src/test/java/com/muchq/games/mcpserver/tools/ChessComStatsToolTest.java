package com.muchq.games.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.StatsResponse;
import java.util.Optional;
import org.junit.jupiter.api.Test;

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

  private final StatsResponse emptyStatsResponse =
      new StatsResponse(null, null, null, null, 0, null);
  private final ChessClient stubClient = new StubChessClient(Optional.of(emptyStatsResponse));
  private final ChessComStatsTool tool = new ChessComStatsTool(stubClient);

  @Test
  public void testExecuteWithValidParameters() {
    String result = ToolResultText.payloadOf(tool.chessComStats("hikaru"));
    assertThat(result).isNotNull();
  }

  @Test
  public void testExecuteWithDifferentUsername() {
    String result = ToolResultText.payloadOf(tool.chessComStats("magnus"));
    assertThat(result).isNotNull();
  }

  @Test
  public void testExecuteWithPlayerNotFound() {
    ChessClient notFoundClient = new StubChessClient(Optional.empty());
    ChessComStatsTool notFoundTool = new ChessComStatsTool(notFoundClient);

    String result = ToolResultText.errorPayloadOf(notFoundTool.chessComStats("nonexistent"));
    assertThat(result).isEqualTo("{\"error\":\"player not found\"}");
  }
}
