package com.muchq.games.mcpserver.tools;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.muchq.games.chessql.parser.ParseException;
import com.muchq.games.one_d4.api.dto.GameFeatureRow;
import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class QueryGamesTool implements McpTool {

  static final int DEFAULT_LIMIT = 10;
  static final int MAX_LIMIT = 50;

  private final IndexerFacade facade;
  private final ObjectMapper mapper;

  public QueryGamesTool(IndexerFacade facade, ObjectMapper mapper) {
    this.facade = facade;
    this.mapper = mapper;
  }

  @Override
  public String getName() {
    return "query_chess_games";
  }

  @Override
  public String getDescription() {
    return "Search indexed games using ChessQL (index first with index_chess_games). Example"
        + " queries: 'white.username = \"hikaru\" AND motif(fork)', 'black.title = \"GM\" AND"
        + " opening.family = \"Caro Kann Defense\"', 'eco = \"B90\" AND NOT motif(pin)'."
        + " Available fields: white.elo, black.elo, white.username, black.username, white.title,"
        + " black.title, time.class, eco, opening.name, opening.family, result, num.moves,"
        + " platform, date (ISO comparisons, e.g. 'date >= \"2026-07-01\"'), and month"
        + " (equality only, 'month = \"2026-07\"'). Note: opening.family is derived from"
        + " chess.com ECO-URL strings, not a normalized taxonomy — 'Closed Sicilian' and 'Closed"
        + " Sicilian Defense' are distinct values. With the player parameter, perspective fields"
        + " work regardless of color:"
        + " me.color, me.elo, me.title, opponent.username, opponent.elo, opponent.title, and"
        + " outcome (win/loss/draw) — e.g. player: hikaru with 'outcome = \"win\" AND"
        + " opponent.title = \"GM\"'. Available motifs: pin, cross_pin, fork, skewer,"
        + " discovered_attack, discovered_check, check, checkmate, promotion,"
        + " promotion_with_check, promotion_with_checkmate, back_rank_mate, smothered_mate,"
        + " double_check.";
  }

  @Override
  public Map<String, Object> getInputSchema() {
    Map<String, Object> properties = new LinkedHashMap<>();
    properties.put("query", Map.of("type", "string", "description", "A ChessQL query string"));
    properties.put(
        "player",
        Map.of(
            "type",
            "string",
            "description",
            "chess.com username that perspective fields (me.*, opponent.*, outcome) are resolved"
                + " against; required when the query uses them"));
    properties.put(
        "limit",
        Map.of(
            "type",
            "integer",
            "description",
            "Maximum games to return (default " + DEFAULT_LIMIT + ", max " + MAX_LIMIT + ")"));
    properties.put(
        "include_pgn",
        Map.of(
            "type",
            "boolean",
            "description",
            "Include the full PGN of each game (large). Default false"));
    return Map.of("type", "object", "properties", properties, "required", List.of("query"));
  }

  @Override
  public String execute(Map<String, Object> arguments) {
    Object rawQuery = arguments.get("query");
    if (rawQuery == null || rawQuery.toString().isBlank()) {
      return ToolResponses.error(mapper, "query is required");
    }
    int limit = clampLimit(arguments.get("limit"));
    boolean includePgn = Boolean.parseBoolean(String.valueOf(arguments.get("include_pgn")));
    Object rawPlayer = arguments.get("player");
    String player = rawPlayer == null ? null : rawPlayer.toString();

    List<GameFeatureRow> games;
    try {
      games = facade.query(rawQuery.toString(), player, limit);
    } catch (ParseException | IllegalArgumentException e) {
      return ToolResponses.error(mapper, e.getMessage());
    }

    ObjectNode result = mapper.createObjectNode();
    ArrayNode gamesNode = result.putArray("games");
    for (GameFeatureRow game : games) {
      ObjectNode gameNode = mapper.valueToTree(game);
      if (!includePgn) {
        gameNode.remove("pgn");
      }
      gamesNode.add(gameNode);
    }
    result.put("count", games.size());

    try {
      return mapper.writeValueAsString(result);
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  private static int clampLimit(Object raw) {
    if (raw == null) {
      return DEFAULT_LIMIT;
    }
    int parsed;
    if (raw instanceof Number n) {
      parsed = n.intValue();
    } else {
      try {
        parsed = Integer.parseInt(raw.toString().trim());
      } catch (NumberFormatException e) {
        return DEFAULT_LIMIT;
      }
    }
    return Math.min(Math.max(parsed, 1), MAX_LIMIT);
  }
}
