package com.muchq.mcpserver;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.mcpserver.dtos.ContentItem;
import com.muchq.mcpserver.dtos.InitializeResult;
import com.muchq.mcpserver.dtos.JsonRpcError;
import com.muchq.mcpserver.dtos.JsonRpcRequest;
import com.muchq.mcpserver.dtos.JsonRpcResponse;
import com.muchq.mcpserver.dtos.ServerCapabilities;
import com.muchq.mcpserver.dtos.ServerInfo;
import com.muchq.mcpserver.dtos.ToolCallParams;
import com.muchq.mcpserver.dtos.ToolCallResult;
import com.muchq.mcpserver.dtos.ToolsCapability;
import com.muchq.mcpserver.dtos.ToolsListResult;
import com.muchq.mcpserver.tools.ToolRegistry;
import jakarta.inject.Inject;
import jakarta.inject.Singleton;
import java.util.List;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class McpRequestHandler {
  private static final Logger LOG = LoggerFactory.getLogger(McpRequestHandler.class);
  private static final String PROTOCOL_VERSION = "2024-11-05";

  private final ObjectMapper objectMapper;
  private final ToolRegistry toolRegistry;

  @Inject
  public McpRequestHandler(ObjectMapper objectMapper, ToolRegistry toolRegistry) {
    this.objectMapper = objectMapper;
    this.toolRegistry = toolRegistry;
  }

  public JsonRpcResponse handleRequest(JsonRpcRequest request) {
    if (!"2.0".equals(request.jsonrpc())) {
      return createErrorResponse(request.id(), -32600, "Invalid Request: jsonrpc must be 2.0");
    }

    return switch (request.method()) {
      case "initialize" -> handleInitialize(request);
      case "tools/list" -> handleToolsList(request);
      case "tools/call" -> handleToolsCall(request);
      default -> createErrorResponse(request.id(), -32601, "Method not found: " + request.method());
    };
  }

  private JsonRpcResponse handleInitialize(JsonRpcRequest request) {
    InitializeResult result =
        new InitializeResult(
            PROTOCOL_VERSION,
            new ServerCapabilities(new ToolsCapability(true)),
            new ServerInfo("micronaut-mcp-server", "1.0.0"));

    return new JsonRpcResponse("2.0", request.id(), result, null);
  }

  private JsonRpcResponse handleToolsList(JsonRpcRequest request) {
    ToolsListResult result = new ToolsListResult(toolRegistry.getTools());
    return new JsonRpcResponse("2.0", request.id(), result, null);
  }

  private JsonRpcResponse handleToolsCall(JsonRpcRequest request) {
    try {
      ToolCallParams params = objectMapper.convertValue(request.params(), ToolCallParams.class);

      String toolResult = toolRegistry.executeTool(params.name(), params.arguments());

      ToolCallResult result = new ToolCallResult(List.of(new ContentItem("text", toolResult)));

      return new JsonRpcResponse("2.0", request.id(), result, null);
    } catch (Exception e) {
      LOG.error("Error executing tool", e);
      return createErrorResponse(request.id(), -32603, "Internal error: " + e.getMessage());
    }
  }

  private JsonRpcResponse createErrorResponse(Object id, int code, String message) {
    JsonRpcError error = new JsonRpcError(code, message);
    return new JsonRpcResponse("2.0", id, null, error);
  }
}
