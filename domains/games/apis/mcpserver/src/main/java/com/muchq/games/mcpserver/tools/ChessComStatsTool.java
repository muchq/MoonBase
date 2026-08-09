package com.muchq.games.mcpserver.tools;

import com.muchq.games.chess_com_client.ChessClient;
import io.micronaut.mcp.annotations.Tool;
import io.micronaut.mcp.annotations.ToolArg;
import jakarta.inject.Singleton;

@Singleton
public class ChessComStatsTool {

  private final ChessClient chessClient;

  public ChessComStatsTool(ChessClient chessClient) {
    this.chessClient = chessClient;
  }

  @Tool(
      name = "chess_com_stats",
      description = "Returns the requested user's chess.com player stats")
  public String chessComStats(
      @ToolArg(description = "The player's chess.com username") String username) {
    if (username.isBlank()) {
      return ToolJson.error("username is required");
    }
    var statsMaybe = chessClient.fetchStats(username);
    if (statsMaybe.isEmpty()) {
      return ToolJson.error("player not found");
    }
    return ToolJson.write(statsMaybe.get());
  }
}
