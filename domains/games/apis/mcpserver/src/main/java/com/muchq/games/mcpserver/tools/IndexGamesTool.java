package com.muchq.games.mcpserver.tools;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.games.chess_com_client.ChessComApiException;
import com.muchq.games.one_d4.api.dto.IndexResponse;
import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class IndexGamesTool implements McpTool {

  private final IndexerFacade facade;
  private final ObjectMapper mapper;

  public IndexGamesTool(IndexerFacade facade, ObjectMapper mapper) {
    this.facade = facade;
    this.mapper = mapper;
  }

  @Override
  public String getName() {
    return "index_chess_games";
  }

  @Override
  public String getDescription() {
    return "Index a chess player's games for tactical motif detection and ChessQL queries."
        + " Fetches games from chess.com, replays positions, and detects pins, forks, skewers,"
        + " discovered attacks, checks, checkmates, promotions, and more. Single-month requests"
        + " complete synchronously; multi-month requests run in the background — check progress"
        + " with index_status. Once indexed, use query_chess_games and aggregate_chess_games.";
  }

  @Override
  public Map<String, Object> getInputSchema() {
    Map<String, Object> properties = new LinkedHashMap<>();
    properties.put("username", Map.of("type", "string", "description", "Chess platform username"));
    properties.put(
        "platform",
        Map.of(
            "type",
            "string",
            "enum",
            List.of("chess.com"),
            "description",
            "Chess platform (currently only chess.com)"));
    properties.put(
        "start_month",
        Map.of("type", "string", "description", "Start month in YYYY-MM format (e.g. 2026-03)"));
    properties.put(
        "end_month",
        Map.of("type", "string", "description", "End month in YYYY-MM format (e.g. 2026-03)"));
    properties.put(
        "exclude_bullet",
        Map.of("type", "boolean", "description", "Skip bullet games. Default false"));
    return Map.of(
        "type",
        "object",
        "properties",
        properties,
        "required",
        List.of("username", "platform", "start_month", "end_month"));
  }

  @Override
  public String execute(Map<String, Object> arguments) {
    String username = str(arguments, "username");
    String platform = str(arguments, "platform");
    String startMonth = str(arguments, "start_month");
    String endMonth = str(arguments, "end_month");
    boolean excludeBullet = Boolean.parseBoolean(String.valueOf(arguments.get("exclude_bullet")));

    try {
      IndexResponse result = facade.index(username, platform, startMonth, endMonth, excludeBullet);
      return mapper.writeValueAsString(result);
    } catch (IllegalArgumentException e) {
      return ToolResponses.error(mapper, e.getMessage());
    } catch (ChessComApiException e) {
      return ToolResponses.error(
          mapper, "chess.com API error (HTTP " + e.statusCode() + "): " + e.getMessage());
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  private static String str(Map<String, Object> arguments, String key) {
    Object value = arguments.get(key);
    return value == null ? null : value.toString();
  }
}
