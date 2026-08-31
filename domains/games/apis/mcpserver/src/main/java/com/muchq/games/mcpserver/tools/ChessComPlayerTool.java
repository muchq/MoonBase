package com.muchq.games.mcpserver.tools;

import com.muchq.games.chess_com_client.ChessClient;
import io.micronaut.mcp.annotations.Tool;
import io.micronaut.mcp.annotations.ToolArg;
import io.modelcontextprotocol.spec.McpSchema.CallToolResult;
import jakarta.inject.Singleton;

@Singleton
public class ChessComPlayerTool {

  private final ChessClient chessClient;

  public ChessComPlayerTool(ChessClient chessClient) {
    this.chessClient = chessClient;
  }

  @Tool(
      name = "chess_com_player",
      description = "Returns the requested user's chess.com player information")
  public CallToolResult chessComPlayer(
      @ToolArg(description = "The player's chess.com username") String username) {
    return ToolCallLog.logged(
        "chess_com_player",
        () -> {
          if (username.isBlank()) {
            return ToolResults.error("username is required");
          }
          var playerMaybe = chessClient.fetchPlayer(username);
          if (playerMaybe.isEmpty()) {
            return ToolResults.error("player not found");
          }
          return ToolResults.ok(playerMaybe.get());
        });
  }
}
