package com.muchq.games.mcpserver.tools;

import com.muchq.games.one_d4.api.dto.IndexResponse;
import io.micronaut.mcp.annotations.Tool;
import io.micronaut.mcp.annotations.ToolArg;
import io.modelcontextprotocol.spec.McpSchema.CallToolResult;
import jakarta.inject.Singleton;
import java.util.Optional;
import java.util.UUID;

@Singleton
public class IndexStatusTool {

  private final IndexerFacade facade;

  public IndexStatusTool(IndexerFacade facade) {
    this.facade = facade;
  }

  @Tool(
      name = "index_status",
      description =
          "Check the status of an indexing request started with index_chess_games. Returns the"
              + " current status (PENDING/PROCESSING/COMPLETED/FAILED), games indexed so far, and"
              + " any error message.")
  public CallToolResult indexStatus(
      @ToolArg(name = "request_id", description = "The UUID of the indexing request")
          String requestId) {
    return ToolCallLog.logged("index_status", () -> doIndexStatus(requestId));
  }

  private CallToolResult doIndexStatus(String requestId) {
    if (requestId.isBlank()) {
      return ToolResults.error("request_id is required");
    }
    UUID parsed;
    try {
      parsed = UUID.fromString(requestId.trim());
    } catch (IllegalArgumentException e) {
      return ToolResults.error("invalid request_id: '" + requestId + "' (expected a UUID)");
    }

    // Separate try: the lookup is the part that talks to one_d4, and folding it into the one above
    // would report an outage as "invalid request_id" — a message that sends the caller to fix an
    // argument that was fine.
    Optional<IndexResponse> status;
    try {
      status = facade.status(parsed);
    } catch (IllegalArgumentException | OneD4Client.UpstreamException e) {
      return ToolResults.error(e.getMessage());
    }
    if (status.isEmpty()) {
      return ToolResults.error("indexing request not found: " + parsed);
    }
    return ToolResults.ok(status.get());
  }
}
