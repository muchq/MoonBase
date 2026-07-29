package com.muchq.games.mcpserver.tools;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.muchq.games.chessql.parser.ParseException;
import com.muchq.games.one_d4.api.dto.AggregateRow;
import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class AggregateGamesTool implements McpTool {

  static final int DEFAULT_LIMIT = 20;
  static final int MAX_LIMIT = 100;

  private final IndexerFacade facade;
  private final ObjectMapper mapper;

  public AggregateGamesTool(IndexerFacade facade, ObjectMapper mapper) {
    this.facade = facade;
    this.mapper = mapper;
  }

  @Override
  public String getName() {
    return "aggregate_chess_games";
  }

  @Override
  public String getDescription() {
    return "Count indexed games grouped by one or more fields, filtered by a ChessQL query"
        + " (index first with index_chess_games). This answers questions like 'most popular"
        + " openings' in one call: query 'white.username = \"hikaru\" AND time.class ="
        + " \"blitz\"' with group_by [\"opening_family\"]. With the player parameter the filter"
        + " may use perspective fields (me.*, opponent.*, outcome) — e.g. player: hikaru with"
        + " query 'me.color = \"white\" AND opponent.title = \"GM\"'. Groupable fields (physical"
        + " columns only): opening_family, opening_name, eco, result, time_class, white_title,"
        + " black_title, white_username, black_username, platform.";
  }

  @Override
  public Map<String, Object> getInputSchema() {
    Map<String, Object> properties = new LinkedHashMap<>();
    properties.put("query", Map.of("type", "string", "description", "A ChessQL filter"));
    properties.put(
        "player",
        Map.of(
            "type",
            "string",
            "description",
            "chess.com username that perspective fields (me.*, opponent.*, outcome) in the"
                + " filter are resolved against; required when the filter uses them"));
    properties.put(
        "group_by",
        Map.of(
            "type",
            "array",
            "items",
            Map.of("type", "string"),
            "description",
            "Fields to group by, e.g. [\"opening_family\"]"));
    properties.put(
        "limit",
        Map.of(
            "type",
            "integer",
            "description",
            "Maximum groups to return (default " + DEFAULT_LIMIT + ", max " + MAX_LIMIT + ")"));
    return Map.of(
        "type", "object", "properties", properties, "required", List.of("query", "group_by"));
  }

  @Override
  public String execute(Map<String, Object> arguments) {
    Object rawQuery = arguments.get("query");
    if (rawQuery == null || rawQuery.toString().isBlank()) {
      return ToolResponses.error(mapper, "query is required");
    }
    Object rawGroupBy = arguments.get("group_by");
    if (!(rawGroupBy instanceof List<?> groupByList) || groupByList.isEmpty()) {
      return ToolResponses.error(mapper, "group_by must be a non-empty array of field names");
    }
    List<String> groupBy = groupByList.stream().map(String::valueOf).toList();
    int limit = clampLimit(arguments.get("limit"));
    Object rawPlayer = arguments.get("player");
    String player = rawPlayer == null ? null : rawPlayer.toString();

    List<AggregateRow> groups;
    try {
      groups = facade.aggregate(rawQuery.toString(), groupBy, player, limit);
    } catch (ParseException | IllegalArgumentException e) {
      return ToolResponses.error(mapper, e.getMessage());
    }

    // Built as a node so "groups" stays present even when empty, regardless of the mapper's
    // serialization-inclusion configuration.
    ObjectNode result = mapper.createObjectNode();
    ArrayNode groupsNode = result.putArray("groups");
    for (AggregateRow group : groups) {
      groupsNode.add(mapper.<ObjectNode>valueToTree(group));
    }
    result.put("count", groups.size());
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
