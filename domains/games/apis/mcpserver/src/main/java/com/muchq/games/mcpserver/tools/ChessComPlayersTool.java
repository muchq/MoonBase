package com.muchq.games.mcpserver.tools;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.ChessComApiException;
import com.muchq.games.chess_com_client.Player;
import io.micronaut.mcp.annotations.Tool;
import io.micronaut.mcp.annotations.ToolArg;
import io.modelcontextprotocol.spec.McpSchema.CallToolResult;
import jakarta.inject.Named;
import jakarta.inject.Singleton;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
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
@Singleton
public class ChessComPlayersTool {

  static final int MAX_USERNAMES = 50;

  private final ChessClient chessClient;
  private final ExecutorService lookupExecutor;

  public ChessComPlayersTool(
      ChessClient chessClient, @Named("chessComLookup") ExecutorService lookupExecutor) {
    this.chessClient = chessClient;
    this.lookupExecutor = lookupExecutor;
  }

  @Tool(
      name = "chess_com_players",
      description =
          "Returns chess.com player information (including title, if any) for a batch of up to 50"
              + " usernames in one call. The response maps each lowercased username to its"
              + " profile; unknown usernames are listed under not_found.")
  public CallToolResult chessComPlayers(
      @ToolArg(description = "The chess.com usernames to look up (max 50)") List<?> usernames) {
    return ToolCallLog.logged("chess_com_players", () -> doChessComPlayers(usernames));
  }

  private CallToolResult doChessComPlayers(List<?> usernames) {
    if (usernames.isEmpty()) {
      return ToolResults.error("usernames must be a non-empty array of strings");
    }

    // List<?>, not List<String>: the argument binder converts the array to a List but leaves the
    // elements alone, so a JSON number arrives as an Integer and a declared List<String> would
    // throw ClassCastException out of this loop — a 500 where the caller deserves an error message.
    Set<String> deduped = new LinkedHashSet<>();
    for (Object item : usernames) {
      if (item == null || item.toString().isBlank()) {
        return ToolResults.error("usernames must not contain blank entries");
      }
      deduped.add(item.toString().toLowerCase());
    }
    if (deduped.size() > MAX_USERNAMES) {
      return ToolResults.error(
          "too many usernames: " + deduped.size() + " (max " + MAX_USERNAMES + " per call)");
    }

    List<String> ordered = new ArrayList<>(deduped);
    List<Future<Lookup>> futures = new ArrayList<>(ordered.size());
    for (String username : ordered) {
      futures.add(lookupExecutor.submit(() -> lookup(username)));
    }

    ObjectNode result = ToolJson.object();
    ObjectNode players = result.putObject("players");
    var notFound = result.putArray("not_found");
    ObjectNode errors = ToolJson.object();

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
        players.set(username, ToolJson.mapper().valueToTree(lookup.player()));
      }
    }

    if (!errors.isEmpty()) {
      result.set("errors", errors);
    }

    return ToolResults.ok(result);
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
