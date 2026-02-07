package com.muchq.games.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.Player;
import com.muchq.platform.json.JsonUtils;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import org.junit.Test;

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
          List.of());
  private final ChessClient stubClient = new StubChessClient(Optional.of(emptyPlayer));
  private final ChessComPlayerTool tool = new ChessComPlayerTool(stubClient, JsonUtils.mapper());

  @Test
  public void testGetName() {
    assertThat(tool.getName()).isEqualTo("chess_com_player");
  }

  @Test
  public void testGetDescription() {
    assertThat(tool.getDescription()).contains("chess.com").contains("player");
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
    assertThat(result).contains("testuser");
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
    ChessComPlayerTool notFoundTool = new ChessComPlayerTool(notFoundClient, JsonUtils.mapper());

    Map<String, Object> arguments = Map.of("username", "nonexistent");
    String result = notFoundTool.execute(arguments);
    assertThat(result).isEqualTo("player not found");
  }
}
