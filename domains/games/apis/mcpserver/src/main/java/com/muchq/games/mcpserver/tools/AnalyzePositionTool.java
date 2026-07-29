package com.muchq.games.mcpserver.tools;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.io.IOException;
import java.util.List;
import java.util.Map;

public class AnalyzePositionTool implements McpTool {

  private final IndexerFacade facade;
  private final ObjectMapper mapper;

  public AnalyzePositionTool(IndexerFacade facade, ObjectMapper mapper) {
    this.facade = facade;
    this.mapper = mapper;
  }

  @Override
  public String getName() {
    return "analyze_position";
  }

  @Override
  public String getDescription() {
    return "Detect tactical motifs (pins, forks, skewers, discovered attacks, checks,"
        + " checkmates, promotions, ...) in a single PGN without indexing it. Returns the"
        + " detected motifs with per-move occurrence details.";
  }

  @Override
  public Map<String, Object> getInputSchema() {
    return Map.of(
        "type", "object",
        "properties",
            Map.of(
                "pgn",
                Map.of("type", "string", "description", "A PGN string of the game to analyze")),
        "required", List.of("pgn"));
  }

  @Override
  public String execute(Map<String, Object> arguments) {
    Object pgn = arguments.get("pgn");
    if (pgn == null || pgn.toString().isBlank()) {
      return ToolResponses.error(mapper, "pgn is required");
    }

    IndexerFacade.AnalysisResult result;
    try {
      result = facade.analyze(pgn.toString());
    } catch (IllegalArgumentException e) {
      return ToolResponses.error(mapper, e.getMessage());
    }

    // Built as a node so motifs/occurrences stay present even when empty, regardless of the
    // mapper's serialization-inclusion configuration.
    ObjectNode node = mapper.createObjectNode();
    node.put("numMoves", result.numMoves());
    node.set("motifs", mapper.valueToTree(result.motifs()));
    node.set("occurrences", mapper.valueToTree(result.occurrences()));
    try {
      return mapper.writeValueAsString(node);
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }
}
