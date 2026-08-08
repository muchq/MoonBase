package com.muchq.games.mcpserver.tools;

import com.muchq.games.chess_com_client.ChessClient;
import io.micronaut.mcp.annotations.Tool;
import io.micronaut.mcp.annotations.ToolArg;
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
  public String chessComPlayer(
      @ToolArg(description = "The player's chess.com username") String username) {
    if (username.isBlank()) {
      return ToolJson.error("username is required");
    }
    var playerMaybe = chessClient.fetchPlayer(username);
    if (playerMaybe.isEmpty()) {
      return ToolJson.error("player not found");
    }
    return ToolJson.write(playerMaybe.get());
  }
}
