package com.muchq.mcpserver;

import com.muchq.mcpserver.dtos.JsonRpcError;
import com.muchq.mcpserver.dtos.JsonRpcRequest;
import com.muchq.mcpserver.dtos.JsonRpcResponse;
import io.micronaut.http.HttpRequest;
import io.micronaut.http.annotation.Body;
import io.micronaut.http.annotation.Controller;
import io.micronaut.http.annotation.Error;
import io.micronaut.http.annotation.Post;
import jakarta.inject.Inject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Controller("/mcp")
public class McpController {
  private static final Logger LOG = LoggerFactory.getLogger(McpController.class);

  private final McpRequestHandler requestHandler;

  @Inject
  public McpController(McpRequestHandler requestHandler) {
    this.requestHandler = requestHandler;
  }

  @Post
  public JsonRpcResponse handleRequest(@Body JsonRpcRequest request) {
    LOG.info("Received MCP request: method={}, id={}", request.method(), request.id());
    return requestHandler.handleRequest(request);
  }

  @Error(global = true)
  public JsonRpcResponse handleError(HttpRequest<?> request, Throwable error) {
    LOG.error("Error processing MCP request", error);
    return new JsonRpcResponse(
        "2.0", null, null, new JsonRpcError(-32603, "Internal error: " + error.getMessage()));
  }
}
