package com.muchq.games.mcpserver.tools;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.games.chess_com_client.ChessClient;
import java.io.IOException;
import java.util.List;
import java.util.Map;
import java.util.Objects;

public class ChessComPlayerTool implements McpTool {

  private final ChessClient chessClient;
  private final ObjectMapper mapper;

  public ChessComPlayerTool(ChessClient chessClient, ObjectMapper mapper) {
    this.chessClient = chessClient;
    this.mapper = mapper;
  }

  @Override
  public String getName() {
    return "chess_com_player";
  }

  @Override
  public String getDescription() {
    return "Returns the requested user's chess.com player information";
  }

  @Override
  public Map<String, Object> getInputSchema() {
    return Map.of(
        "type", "object",
        "properties",
            Map.of(
                "username",
                Map.of("type", "string", "description", "The player's chess.com username")),
        "required", List.of("username"));
  }

  @Override
  public String execute(Map<String, Object> arguments) {
    String player = (String) arguments.get("username");
    Objects.requireNonNull(player, "username cannot be be null");
    var playerMaybe = chessClient.fetchPlayer(player);
    if (playerMaybe.isEmpty()) {
      return "player not found";
    }

    try {
      return mapper.writeValueAsString(playerMaybe.get());
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }
}
