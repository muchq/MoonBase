package com.muchq.games.mcpserver.tools;

import com.fasterxml.jackson.databind.node.ObjectNode;
import io.micronaut.mcp.annotations.Tool;
import io.micronaut.mcp.annotations.ToolArg;
import io.modelcontextprotocol.spec.McpSchema.CallToolResult;
import jakarta.inject.Singleton;

@Singleton
public class AnalyzePositionTool {

  private final IndexerFacade facade;

  public AnalyzePositionTool(IndexerFacade facade) {
    this.facade = facade;
  }

  @Tool(
      name = "analyze_position",
      description =
          "Detect tactical motifs (pins, forks, skewers, discovered attacks, checks, checkmates,"
              + " promotions, ...) in a single PGN without indexing it. Returns the detected"
              + " motifs with per-move occurrence details.")
  public CallToolResult analyzePosition(
      @ToolArg(description = "A PGN string of the game to analyze") String pgn) {
    if (pgn.isBlank()) {
      return ToolResults.error("pgn is required");
    }

    com.muchq.games.one_d4.api.dto.AnalyzeResponse result;
    try {
      result = facade.analyze(pgn);
    } catch (IllegalArgumentException | OneD4Client.UpstreamException e) {
      return ToolResults.error(e.getMessage());
    }

    // Built as a node so motifs/occurrences stay present even when empty, regardless of the
    // mapper's serialization-inclusion configuration.
    ObjectNode node = ToolJson.object();
    node.put("numMoves", result.numMoves());
    node.set("motifs", ToolJson.mapper().valueToTree(result.motifs()));
    node.set("occurrences", ToolJson.mapper().valueToTree(result.occurrences()));
    return ToolResults.ok(node);
  }
}
