package com.muchq.games.mcpserver.tools;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.PlayedGame;
import com.muchq.games.chess_com_client.PlayerResult;
import java.io.IOException;
import java.time.YearMonth;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class ChessComGamesTool implements McpTool {

  private static final Set<String> TIME_CLASSES = Set.of("blitz", "bullet", "rapid", "daily");
  private static final Set<String> COLORS = Set.of("white", "black");
  private static final String RULES_ALL = "all";
  private static final int DEFAULT_LIMIT = 100;
  private static final int MAX_LIMIT = 1000;

  private final ChessClient chessClient;
  private final ObjectMapper mapper;

  public ChessComGamesTool(ChessClient chessClient, ObjectMapper mapper) {
    this.chessClient = chessClient;
    this.mapper = mapper;
  }

  @Override
  public String getName() {
    return "chess_com_games";
  }

  @Override
  public String getDescription() {
    return "Returns the requested user's chess.com games for the specified month and year. For"
        + " example, username: hikaru, year: 2025, month: 01. Games can be filtered by"
        + " time_class, color, rated, rules, and opponent. Bulky pgn/tcn fields are omitted"
        + " unless include_pgn/include_tcn are set.";
  }

  @Override
  public Map<String, Object> getInputSchema() {
    Map<String, Object> properties = new LinkedHashMap<>();
    properties.put(
        "username", Map.of("type", "string", "description", "The player's chess.com username"));
    properties.put(
        "year",
        Map.of("type", "string", "description", "The year the games were played (yyyy format)"));
    properties.put(
        "month",
        Map.of("type", "string", "description", "The month the games were played (MM format)"));
    properties.put(
        "time_class",
        Map.of(
            "type",
            "string",
            "enum",
            List.of("blitz", "bullet", "rapid", "daily"),
            "description",
            "Only include games of this time class"));
    properties.put(
        "color",
        Map.of(
            "type",
            "string",
            "enum",
            List.of("white", "black"),
            "description",
            "Only include games where the requested user played this color"));
    properties.put(
        "rated",
        Map.of("type", "boolean", "description", "Only include rated (true) or casual (false)"));
    properties.put(
        "rules",
        Map.of(
            "type",
            "string",
            "description",
            "Game rules filter. Defaults to \"chess\", which excludes variants like chess960;"
                + " pass a variant name to select it, or \"all\" to include every rule set"));
    properties.put(
        "opponent",
        Map.of(
            "type",
            "string",
            "description",
            "Only include games against this opponent (chess.com username)"));
    properties.put(
        "include_pgn",
        Map.of(
            "type",
            "boolean",
            "description",
            "Include the full PGN of each game (large). Default false"));
    properties.put(
        "include_tcn",
        Map.of(
            "type",
            "boolean",
            "description",
            "Include the tcn move encoding of each game (large). Default false"));
    properties.put(
        "limit",
        Map.of(
            "type",
            "integer",
            "description",
            "Max games to return after filtering. Default 100, max " + MAX_LIMIT));
    properties.put(
        "offset",
        Map.of("type", "integer", "description", "Games to skip after filtering. Default 0"));

    return Map.of(
        "type",
        "object",
        "properties",
        properties,
        "required",
        List.of("username", "year", "month"));
  }

  @Override
  public String execute(Map<String, Object> arguments) {
    String username = stringArg(arguments, "username");
    if (username == null || username.isBlank()) {
      return ToolResponses.error(mapper, "username is required");
    }

    int year;
    int month;
    try {
      year = parseIntArg(arguments, "year", 1900, 2999);
      month = parseIntArg(arguments, "month", 1, 12);
    } catch (IllegalArgumentException e) {
      return ToolResponses.error(mapper, e.getMessage());
    }

    String timeClass = lowerOrNull(stringArg(arguments, "time_class"));
    if (timeClass != null && !TIME_CLASSES.contains(timeClass)) {
      return ToolResponses.error(
          mapper, "invalid time_class: '" + timeClass + "' (expected one of " + TIME_CLASSES + ")");
    }
    String color = lowerOrNull(stringArg(arguments, "color"));
    if (color != null && !COLORS.contains(color)) {
      return ToolResponses.error(
          mapper, "invalid color: '" + color + "' (expected white or black)");
    }
    Boolean rated = boolArg(arguments, "rated");
    String rulesArg = lowerOrNull(stringArg(arguments, "rules"));
    String rules = rulesArg == null ? "chess" : rulesArg;
    String opponent = lowerOrNull(stringArg(arguments, "opponent"));
    boolean includePgn = Boolean.TRUE.equals(boolArg(arguments, "include_pgn"));
    boolean includeTcn = Boolean.TRUE.equals(boolArg(arguments, "include_tcn"));

    int limit;
    int offset;
    try {
      Integer limitArg = intArgOrNull(arguments, "limit");
      Integer offsetArg = intArgOrNull(arguments, "offset");
      limit = limitArg == null ? DEFAULT_LIMIT : Math.min(Math.max(limitArg, 1), MAX_LIMIT);
      offset = offsetArg == null ? 0 : Math.max(offsetArg, 0);
    } catch (IllegalArgumentException e) {
      return ToolResponses.error(mapper, e.getMessage());
    }

    var gamesMaybe = chessClient.fetchGames(username, YearMonth.of(year, month));
    if (gamesMaybe.isEmpty()) {
      return ToolResponses.error(mapper, "player not found");
    }

    String usernameLower = username.toLowerCase();
    List<PlayedGame> matching =
        gamesMaybe.get().games().stream()
            .filter(g -> RULES_ALL.equals(rules) || rules.equalsIgnoreCase(g.rules()))
            .filter(g -> timeClass == null || timeClass.equalsIgnoreCase(g.timeClass()))
            .filter(g -> rated == null || rated == g.rated())
            .filter(g -> color == null || playedAs(g, usernameLower, color))
            .filter(g -> opponent == null || playedAgainst(g, usernameLower, opponent))
            .toList();

    List<PlayedGame> page = matching.stream().skip(offset).limit(limit).toList();

    ObjectNode result = mapper.createObjectNode();
    ArrayNode gamesNode = result.putArray("games");
    for (PlayedGame game : page) {
      ObjectNode gameNode = mapper.valueToTree(game);
      if (!includePgn) {
        gameNode.remove("pgn");
      }
      if (!includeTcn) {
        gameNode.remove("tcn");
      }
      gamesNode.add(gameNode);
    }
    result.put("total_matching", matching.size());
    result.put("returned", page.size());
    result.put("offset", offset);
    result.put("has_more", offset + page.size() < matching.size());

    try {
      return mapper.writeValueAsString(result);
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
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

  private static String stringArg(Map<String, Object> arguments, String key) {
    Object value = arguments.get(key);
    return value == null ? null : value.toString();
  }

  private static String lowerOrNull(String value) {
    return value == null || value.isBlank() ? null : value.toLowerCase();
  }

  private static Boolean boolArg(Map<String, Object> arguments, String key) {
    Object value = arguments.get(key);
    if (value == null) {
      return null;
    }
    if (value instanceof Boolean b) {
      return b;
    }
    return Boolean.parseBoolean(value.toString());
  }

  private static Integer intArgOrNull(Map<String, Object> arguments, String key) {
    Object value = arguments.get(key);
    if (value == null) {
      return null;
    }
    if (value instanceof Number n) {
      return n.intValue();
    }
    try {
      return Integer.parseInt(value.toString().trim());
    } catch (NumberFormatException e) {
      throw new IllegalArgumentException(
          "invalid " + key + ": '" + value + "' (expected an integer)");
    }
  }

  private static int parseIntArg(Map<String, Object> arguments, String key, int min, int max) {
    Object value = arguments.get(key);
    if (value == null) {
      throw new IllegalArgumentException(key + " is required");
    }
    int parsed;
    if (value instanceof Number n) {
      parsed = n.intValue();
    } else {
      try {
        parsed = Integer.parseInt(value.toString().trim());
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
    }
    if (parsed < min || parsed > max) {
      throw new IllegalArgumentException(
          "invalid " + key + ": '" + value + "' (expected " + min + "-" + max + ")");
    }
    return parsed;
  }
}
