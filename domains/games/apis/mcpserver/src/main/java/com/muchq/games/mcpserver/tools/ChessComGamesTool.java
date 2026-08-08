package com.muchq.games.mcpserver.tools;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.PlayedGame;
import com.muchq.games.chess_com_client.PlayerResult;
import io.micronaut.core.annotation.Nullable;
import io.micronaut.mcp.annotations.Tool;
import io.micronaut.mcp.annotations.ToolArg;
import jakarta.inject.Singleton;
import java.time.YearMonth;
import java.util.List;
import java.util.Set;

@Singleton
public class ChessComGamesTool {

  private static final Set<String> TIME_CLASSES = Set.of("blitz", "bullet", "rapid", "daily");
  private static final Set<String> COLORS = Set.of("white", "black");
  private static final String RULES_ALL = "all";
  private static final int DEFAULT_LIMIT = 100;
  private static final int MAX_LIMIT = 1000;

  private final ChessClient chessClient;

  public ChessComGamesTool(ChessClient chessClient) {
    this.chessClient = chessClient;
  }

  @Tool(
      name = "chess_com_games",
      description =
          "Returns the requested user's chess.com games for the specified month and year. For"
              + " example, username: hikaru, year: 2025, month: 01. Games can be filtered by"
              + " time_class, color, rated, rules, and opponent. Bulky pgn/tcn fields are omitted"
              + " unless include_pgn/include_tcn are set.")
  public String chessComGames(
      @ToolArg(description = "The player's chess.com username") String username,
      @ToolArg(description = "The year the games were played (yyyy format)") String year,
      @ToolArg(description = "The month the games were played (MM format)") String month,
      @Nullable
          @ToolArg(
              name = "time_class",
              description = "Only include games of this time class: blitz, bullet, rapid or daily")
          String timeClass,
      @Nullable
          @ToolArg(
              description =
                  "Only include games where the requested user played this color: white or black")
          String color,
      @Nullable @ToolArg(description = "Only include rated (true) or casual (false)") Boolean rated,
      @Nullable
          @ToolArg(
              description =
                  "Game rules filter. Defaults to \"chess\", which excludes variants like"
                      + " chess960; pass a variant name to select it, or \"all\" to include every"
                      + " rule set")
          String rules,
      @Nullable
          @ToolArg(description = "Only include games against this opponent (chess.com username)")
          String opponent,
      @Nullable
          @ToolArg(
              name = "include_pgn",
              description = "Include the full PGN of each game (large). Default false")
          Boolean includePgn,
      @Nullable
          @ToolArg(
              name = "include_tcn",
              description = "Include the tcn move encoding of each game (large). Default false")
          Boolean includeTcn,
      @Nullable
          @ToolArg(
              description = "Max games to return after filtering. Default 100, max " + MAX_LIMIT)
          Integer limit,
      @Nullable @ToolArg(description = "Games to skip after filtering. Default 0") Integer offset) {

    if (username.isBlank()) {
      return ToolJson.error("username is required");
    }

    int parsedYear;
    int parsedMonth;
    try {
      parsedYear = parseIntArg("year", year, 1900, 2999);
      parsedMonth = parseIntArg("month", month, 1, 12);
    } catch (IllegalArgumentException e) {
      return ToolJson.error(e.getMessage());
    }

    String normalizedTimeClass = lowerOrNull(timeClass);
    if (normalizedTimeClass != null && !TIME_CLASSES.contains(normalizedTimeClass)) {
      return ToolJson.error(
          "invalid time_class: '"
              + normalizedTimeClass
              + "' (expected one of "
              + TIME_CLASSES
              + ")");
    }
    String normalizedColor = lowerOrNull(color);
    if (normalizedColor != null && !COLORS.contains(normalizedColor)) {
      return ToolJson.error("invalid color: '" + normalizedColor + "' (expected white or black)");
    }
    String normalizedRules = lowerOrNull(rules);
    String effectiveRules = normalizedRules == null ? "chess" : normalizedRules;
    String normalizedOpponent = lowerOrNull(opponent);
    boolean withPgn = Boolean.TRUE.equals(includePgn);
    boolean withTcn = Boolean.TRUE.equals(includeTcn);

    int effectiveLimit = limit == null ? DEFAULT_LIMIT : Math.min(Math.max(limit, 1), MAX_LIMIT);
    int effectiveOffset = offset == null ? 0 : Math.max(offset, 0);

    var gamesMaybe = chessClient.fetchGames(username, YearMonth.of(parsedYear, parsedMonth));
    if (gamesMaybe.isEmpty()) {
      return ToolJson.error("player not found");
    }

    String usernameLower = username.toLowerCase();
    List<PlayedGame> matching =
        gamesMaybe.get().games().stream()
            .filter(
                g -> RULES_ALL.equals(effectiveRules) || effectiveRules.equalsIgnoreCase(g.rules()))
            .filter(
                g ->
                    normalizedTimeClass == null
                        || normalizedTimeClass.equalsIgnoreCase(g.timeClass()))
            .filter(g -> rated == null || rated == g.rated())
            .filter(g -> normalizedColor == null || playedAs(g, usernameLower, normalizedColor))
            .filter(
                g ->
                    normalizedOpponent == null
                        || playedAgainst(g, usernameLower, normalizedOpponent))
            .toList();

    List<PlayedGame> page = matching.stream().skip(effectiveOffset).limit(effectiveLimit).toList();

    ObjectNode result = ToolJson.object();
    ArrayNode gamesNode = result.putArray("games");
    for (PlayedGame game : page) {
      ObjectNode gameNode = ToolJson.mapper().valueToTree(game);
      if (!withPgn) {
        gameNode.remove("pgn");
      }
      if (!withTcn) {
        gameNode.remove("tcn");
      }
      gamesNode.add(gameNode);
    }
    result.put("total_matching", matching.size());
    result.put("returned", page.size());
    result.put("offset", effectiveOffset);
    result.put("has_more", effectiveOffset + page.size() < matching.size());

    return ToolJson.write(result);
  }

  /** True when {@code usernameLower} played the given side in this game. */
  private static boolean playedAs(PlayedGame game, String usernameLower, String color) {
    PlayerResult side = "white".equals(color) ? game.whiteResult() : game.blackResult();
    return side != null
        && side.username() != null
        && side.username().toLowerCase().equals(usernameLower);
  }

  /** True when the side not played by {@code usernameLower} is {@code opponentLower}. */
  private static boolean playedAgainst(
      PlayedGame game, String usernameLower, String opponentLower) {
    String white = usernameOrNull(game.whiteResult());
    String black = usernameOrNull(game.blackResult());
    if (usernameLower.equals(white)) {
      return opponentLower.equals(black);
    }
    if (usernameLower.equals(black)) {
      return opponentLower.equals(white);
    }
    return false;
  }

  private static String usernameOrNull(PlayerResult result) {
    return result != null && result.username() != null ? result.username().toLowerCase() : null;
  }

  private static String lowerOrNull(String value) {
    return value == null || value.isBlank() ? null : value.toLowerCase();
  }

  private static int parseIntArg(String key, String value, int min, int max) {
    if (value == null || value.isBlank()) {
      throw new IllegalArgumentException(key + " is required");
    }
    int parsed;
    try {
      parsed = Integer.parseInt(value.trim());
    } catch (NumberFormatException e) {
      throw new IllegalArgumentException(
          "invalid "
              + key
              + ": '"
              + value
              + "' (expected "
              + ("year".equals(key) ? "yyyy, e.g. 2025" : "MM, e.g. 07")
              + ")");
    }
    if (parsed < min || parsed > max) {
      throw new IllegalArgumentException(
          "invalid " + key + ": '" + value + "' (expected " + min + "-" + max + ")");
    }
    return parsed;
  }
}
