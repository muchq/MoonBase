package com.muchq.games.mcpserver.tools;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.ChessComApiException;
import com.muchq.games.chess_com_client.Player;
import java.io.IOException;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;

/**
 * Batch player lookup: one call resolves profiles (including title) for up to {@link
 * #MAX_USERNAMES} usernames, instead of one MCP round trip per player. Lookups run sequentially so
 * a single call cannot fan out an unbounded number of concurrent chess.com requests.
 */
public class ChessComPlayersTool implements McpTool {

  static final int MAX_USERNAMES = 50;

  private final ChessClient chessClient;
  private final ObjectMapper mapper;

  public ChessComPlayersTool(ChessClient chessClient, ObjectMapper mapper) {
    this.chessClient = chessClient;
    this.mapper = mapper;
  }

  @Override
  public String getName() {
    return "chess_com_players";
  }

  @Override
  public String getDescription() {
    return "Returns chess.com player information (including title, if any) for a batch of up to "
        + MAX_USERNAMES
        + " usernames in one call. The response maps each lowercased username to its profile;"
        + " unknown usernames are listed under not_found.";
  }

  @Override
  public Map<String, Object> getInputSchema() {
    return Map.of(
        "type", "object",
        "properties",
            Map.of(
                "usernames",
                Map.of(
                    "type",
                    "array",
                    "items",
                    Map.of("type", "string"),
                    "maxItems",
                    MAX_USERNAMES,
                    "description",
                    "The chess.com usernames to look up (max " + MAX_USERNAMES + ")")),
        "required", List.of("usernames"));
  }

  @Override
  public String execute(Map<String, Object> arguments) {
    Object raw = arguments.get("usernames");
    if (!(raw instanceof List<?> rawList) || rawList.isEmpty()) {
      return ToolResponses.error(mapper, "usernames must be a non-empty array of strings");
    }

    Set<String> usernames = new LinkedHashSet<>();
    for (Object item : rawList) {
      if (item == null || item.toString().isBlank()) {
        return ToolResponses.error(mapper, "usernames must not contain blank entries");
      }
      usernames.add(item.toString().toLowerCase());
    }
    if (usernames.size() > MAX_USERNAMES) {
      return ToolResponses.error(
          mapper,
          "too many usernames: " + usernames.size() + " (max " + MAX_USERNAMES + " per call)");
    }

    ObjectNode result = mapper.createObjectNode();
    ObjectNode players = result.putObject("players");
    var notFound = result.putArray("not_found");
    ObjectNode errors = mapper.createObjectNode();

    for (String username : usernames) {
      try {
        Optional<Player> player = chessClient.fetchPlayer(username);
        if (player.isPresent()) {
          players.set(username, mapper.valueToTree(player.get()));
        } else {
          notFound.add(username);
        }
      } catch (ChessComApiException e) {
        // Keep the partial batch usable: record the failure per username instead of discarding
        // everything already fetched.
        errors.put(username, "HTTP " + e.statusCode());
      }
    }

    if (!errors.isEmpty()) {
      result.set("errors", errors);
    }

    try {
      return mapper.writeValueAsString(result);
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }
}
