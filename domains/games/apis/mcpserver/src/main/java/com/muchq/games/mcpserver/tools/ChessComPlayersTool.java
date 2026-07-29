package com.muchq.games.mcpserver.tools;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.ChessComApiException;
import com.muchq.games.chess_com_client.Player;
import java.io.IOException;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;

/**
 * Batch player lookup: one call resolves profiles (including title) for up to {@link
 * #MAX_USERNAMES} usernames, instead of one MCP round trip per player. Lookups run concurrently on
 * a small shared pool, so fan-out against chess.com is bounded by the pool size rather than
 * unbounded per call.
 */
public class ChessComPlayersTool implements McpTool {

  static final int MAX_USERNAMES = 50;

  private final ChessClient chessClient;
  private final ObjectMapper mapper;
  private final ExecutorService lookupExecutor;

  public ChessComPlayersTool(
      ChessClient chessClient, ObjectMapper mapper, ExecutorService lookupExecutor) {
    this.chessClient = chessClient;
    this.mapper = mapper;
    this.lookupExecutor = lookupExecutor;
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

    List<String> ordered = new ArrayList<>(usernames);
    List<Future<Lookup>> futures = new ArrayList<>(ordered.size());
    for (String username : ordered) {
      futures.add(lookupExecutor.submit(() -> lookup(username)));
    }

    ObjectNode result = mapper.createObjectNode();
    ObjectNode players = result.putObject("players");
    var notFound = result.putArray("not_found");
    ObjectNode errors = mapper.createObjectNode();

    for (int i = 0; i < futures.size(); i++) {
      String username = ordered.get(i);
      Lookup lookup;
      try {
        lookup = futures.get(i).get();
      } catch (ExecutionException e) {
        errors.put(username, String.valueOf(e.getCause()));
        continue;
      } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
        errors.put(username, "interrupted");
        break;
      }
      if (lookup.error() != null) {
        errors.put(username, lookup.error());
      } else if (lookup.player() == null) {
        notFound.add(username);
      } else {
        players.set(username, mapper.valueToTree(lookup.player()));
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

  /**
   * Resolves one username. API failures are captured per username so a single 429 keeps the partial
   * batch usable instead of discarding everything already fetched.
   */
  private Lookup lookup(String username) {
    try {
      Optional<Player> player = chessClient.fetchPlayer(username);
      return new Lookup(player.orElse(null), null);
    } catch (ChessComApiException e) {
      return new Lookup(null, "HTTP " + e.statusCode());
    }
  }

  private record Lookup(Player player, String error) {}
}
