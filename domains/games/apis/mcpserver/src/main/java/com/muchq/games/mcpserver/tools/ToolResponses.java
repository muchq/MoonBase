package com.muchq.games.mcpserver.tools;

import com.fasterxml.jackson.databind.ObjectMapper;

/** Shared response helpers for MCP tools. */
final class ToolResponses {

  private ToolResponses() {}

  /**
   * Renders an error as a JSON object ({@code {"error": "..."}}) so that failures are
   * machine-distinguishable from successful payloads, which are always JSON too.
   */
  static String error(ObjectMapper mapper, String message) {
    try {
      return mapper.writeValueAsString(java.util.Map.of("error", message));
    } catch (java.io.IOException e) {
      // Map.of("error", message) cannot fail to serialize; guard anyway.
      throw new RuntimeException(e);
    }
  }
}
