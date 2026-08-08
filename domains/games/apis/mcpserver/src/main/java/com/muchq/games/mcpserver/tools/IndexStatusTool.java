package com.muchq.games.mcpserver.tools;

import com.muchq.games.one_d4.api.dto.IndexResponse;
import io.micronaut.mcp.annotations.Tool;
import io.micronaut.mcp.annotations.ToolArg;
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
  public String indexStatus(
      @ToolArg(name = "request_id", description = "The UUID of the indexing request")
          String requestId) {
    if (requestId.isBlank()) {
      return ToolJson.error("request_id is required");
    }
    UUID parsed;
    try {
      parsed = UUID.fromString(requestId.trim());
    } catch (IllegalArgumentException e) {
      return ToolJson.error("invalid request_id: '" + requestId + "' (expected a UUID)");
    }

    Optional<IndexResponse> status = facade.status(parsed);
    if (status.isEmpty()) {
      return ToolJson.error("indexing request not found: " + parsed);
    }
    return ToolJson.write(status.get());
  }
}
