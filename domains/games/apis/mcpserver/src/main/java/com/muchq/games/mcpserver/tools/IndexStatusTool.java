package com.muchq.games.mcpserver.tools;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.games.one_d4.api.dto.IndexResponse;
import java.io.IOException;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;

public class IndexStatusTool implements McpTool {

  private final IndexerFacade facade;
  private final ObjectMapper mapper;

  public IndexStatusTool(IndexerFacade facade, ObjectMapper mapper) {
    this.facade = facade;
    this.mapper = mapper;
  }

  @Override
  public String getName() {
    return "index_status";
  }

  @Override
  public String getDescription() {
    return "Check the status of an indexing request started with index_chess_games. Returns the"
        + " current status (PENDING/PROCESSING/COMPLETED/FAILED), games indexed so far, and any"
        + " error message.";
  }

  @Override
  public Map<String, Object> getInputSchema() {
    return Map.of(
        "type", "object",
        "properties",
            Map.of(
                "request_id",
                Map.of("type", "string", "description", "The UUID of the indexing request")),
        "required", List.of("request_id"));
  }

  @Override
  public String execute(Map<String, Object> arguments) {
    Object raw = arguments.get("request_id");
    if (raw == null || raw.toString().isBlank()) {
      return ToolResponses.error(mapper, "request_id is required");
    }
    UUID requestId;
    try {
      requestId = UUID.fromString(raw.toString().trim());
    } catch (IllegalArgumentException e) {
      return ToolResponses.error(mapper, "invalid request_id: '" + raw + "' (expected a UUID)");
    }

    Optional<IndexResponse> status = facade.status(requestId);
    if (status.isEmpty()) {
      return ToolResponses.error(mapper, "indexing request not found: " + requestId);
    }
    try {
      return mapper.writeValueAsString(status.get());
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }
}
