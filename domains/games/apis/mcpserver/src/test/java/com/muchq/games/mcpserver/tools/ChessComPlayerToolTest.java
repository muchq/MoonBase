package com.muchq.games.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.Player;
import java.time.Instant;
import java.util.List;
import java.util.Optional;
import org.junit.jupiter.api.Test;

public class ChessComPlayerToolTest {

  private static class StubChessClient extends ChessClient {
    private final Optional<Player> response;

    public StubChessClient(Optional<Player> response) {
      super(null, null);
      this.response = response;
    }

    @Override
    public Optional<Player> fetchPlayer(String player) {
      return response;
    }
  }

  private final Player emptyPlayer =
      new Player(
          1,
          "https://api.chess.com/pub/player/testuser",
          "https://chess.com/member/testuser",
          "Test User",
          "testuser",
          100,
          "https://api.chess.com/pub/country/US",
          Instant.now(),
          Instant.now(),
          "active",
          false,
          false,
          "bronze",
          List.of(),
          "GM",
          "Somewhere",
          2650);
  private final ChessClient stubClient = new StubChessClient(Optional.of(emptyPlayer));
  private final ChessComPlayerTool tool = new ChessComPlayerTool(stubClient);

  @Test
  public void testExecuteWithValidParameters() {
    String result = tool.chessComPlayer("hikaru");
    assertThat(result).isNotNull();
    assertThat(result).contains("testuser");
    assertThat(result).contains("\"title\":\"GM\"");
  }

  @Test
  public void testExecuteWithDifferentUsername() {
    String result = tool.chessComPlayer("magnus");
    assertThat(result).isNotNull();
  }

  @Test
  public void testExecuteWithPlayerNotFound() {
    ChessClient notFoundClient = new StubChessClient(Optional.empty());
    ChessComPlayerTool notFoundTool = new ChessComPlayerTool(notFoundClient);

    String result = notFoundTool.chessComPlayer("nonexistent");
    assertThat(result).isEqualTo("{\"error\":\"player not found\"}");
  }
}
