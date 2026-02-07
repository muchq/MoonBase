package com.muchq.games.mcpserver;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.mcpserver.tools.ChessComGamesTool;
import com.muchq.games.mcpserver.tools.ChessComPlayerTool;
import com.muchq.games.mcpserver.tools.ChessComStatsTool;
import com.muchq.games.mcpserver.tools.McpTool;
import com.muchq.games.mcpserver.tools.ServerTimeTool;
import com.muchq.games.mcpserver.tools.ToolRegistry;
import com.muchq.platform.http_client.core.HttpClient;
import com.muchq.platform.http_client.jdk.Jdk11HttpClient;
import com.muchq.platform.json.JsonUtils;
import io.micronaut.context.annotation.Context;
import io.micronaut.context.annotation.Factory;
import java.time.Clock;
import java.util.List;

@Factory
public class McpModule {

  @Context
  public Clock clock() {
    return Clock.systemUTC();
  }

  @Context
  public HttpClient httpClient() {
    return new Jdk11HttpClient(java.net.http.HttpClient.newHttpClient());
  }

  @Context
  public ChessClient chessClient(HttpClient httpClient, ObjectMapper objectMapper) {
    return new ChessClient(httpClient, objectMapper);
  }

  @Context
  public ObjectMapper objectMapper() {
    return JsonUtils.mapper();
  }

  @Context
  public List<McpTool> mcpTools(Clock clock, ChessClient chessClient, ObjectMapper objectMapper) {
    return List.of(
        new ChessComGamesTool(chessClient, objectMapper),
        new ChessComPlayerTool(chessClient, objectMapper),
        new ChessComStatsTool(chessClient, objectMapper),
        new ServerTimeTool(clock));
  }

  @Context
  public ToolRegistry toolRegistry(List<McpTool> tools) {
    return new ToolRegistry(tools);
  }
}
