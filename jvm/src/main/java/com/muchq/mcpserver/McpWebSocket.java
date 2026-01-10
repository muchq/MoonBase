package com.muchq.mcpserver;

import com.fasterxml.jackson.databind.ObjectMapper;
import io.micronaut.websocket.WebSocketSession;
import io.micronaut.websocket.annotation.OnClose;
import io.micronaut.websocket.annotation.OnMessage;
import io.micronaut.websocket.annotation.OnOpen;
import io.micronaut.websocket.annotation.ServerWebSocket;
import jakarta.inject.Inject;
import java.util.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@ServerWebSocket("/mcp")
public class McpWebSocket {
  private static final Logger LOG = LoggerFactory.getLogger(McpWebSocket.class);
  private static final String PROTOCOL_VERSION = "2024-11-05";

  private final ObjectMapper objectMapper;
  private final ToolRegistry toolRegistry;

  @Inject
  public McpWebSocket(ObjectMapper objectMapper, ToolRegistry toolRegistry) {
    this.objectMapper = objectMapper;
    this.toolRegistry = toolRegistry;
  }

  @OnOpen
  public void onOpen(WebSocketSession session) {
    LOG.info("MCP client connected: {}", session.getId());
  }

  @OnMessage
  public String onMessage(String message, WebSocketSession session) {
    LOG.info("Received message: {}", message);

    try {
      JsonRpcRequest request = objectMapper.readValue(message, JsonRpcRequest.class);
      return handleRequest(request);
    } catch (Exception e) {
      LOG.error("Error processing message", e);
      return createErrorResponse(null, -32700, "Parse error");
    }
  }

  @OnClose
  public void onClose(WebSocketSession session) {
    LOG.info("MCP client disconnected: {}", session.getId());
  }

  private String handleRequest(JsonRpcRequest request) throws Exception {
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

  private String handleInitialize(JsonRpcRequest request) throws Exception {
    InitializeResult result =
        new InitializeResult(
            PROTOCOL_VERSION,
            new ServerCapabilities(new ToolsCapability(true)),
            new ServerInfo("micronaut-mcp-server", "1.0.0"));

    JsonRpcResponse response = new JsonRpcResponse("2.0", request.id(), result, null);
    return objectMapper.writeValueAsString(response);
  }

  private String handleToolsList(JsonRpcRequest request) throws Exception {
    ToolsListResult result = new ToolsListResult(toolRegistry.getTools());

    JsonRpcResponse response = new JsonRpcResponse("2.0", request.id(), result, null);
    return objectMapper.writeValueAsString(response);
  }

  private String handleToolsCall(JsonRpcRequest request) throws Exception {
    ToolCallParams params =
        objectMapper.convertValue(request.params(), ToolCallParams.class);

    String toolResult = toolRegistry.executeTool(params.name(), params.arguments());

    ToolCallResult result =
        new ToolCallResult(List.of(new ContentItem("text", toolResult)));

    JsonRpcResponse response = new JsonRpcResponse("2.0", request.id(), result, null);
    return objectMapper.writeValueAsString(response);
  }

  private String createErrorResponse(Object id, int code, String message) {
    try {
      JsonRpcError error = new JsonRpcError(code, message);
      JsonRpcResponse response = new JsonRpcResponse("2.0", id, null, error);
      return objectMapper.writeValueAsString(response);
    } catch (Exception e) {
      LOG.error("Error creating error response", e);
      return "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32603,\"message\":\"Internal error\"}}";
    }
  }
}
