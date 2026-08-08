package com.muchq.games.mcpserver.tools;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.muchq.platform.json.JsonUtils;
import java.io.IOException;
import java.util.Map;

/**
 * JSON rendering for tool payloads.
 *
 * <p>Deliberately not an injected bean. Micronaut's own {@code ObjectMapper} — the one the HTTP
 * layer and the MCP framework use — carries different inclusion defaults, and which bean wins
 * injection must not decide the shape of tool responses. Holding the mapper here makes that
 * structural rather than a wiring convention someone can undo by adding a {@code @Singleton}
 * elsewhere. Tests read the same shapes back through {@link JsonUtils#mapper()}.
 */
final class ToolJson {

  // JsonUtils.mapper() hands out a copy, so this instance is ours. ObjectMapper is thread-safe
  // once configured, and nothing here reconfigures it.
  private static final ObjectMapper MAPPER = JsonUtils.mapper();

  private ToolJson() {}

  static ObjectMapper mapper() {
    return MAPPER;
  }

  static ObjectNode object() {
    return MAPPER.createObjectNode();
  }

  /** Serializes a tool payload. */
  static String write(Object value) {
    try {
      return MAPPER.writeValueAsString(value);
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  /**
   * Renders a caller error as a JSON object ({@code {"error": "..."}}) so that failures are
   * machine-distinguishable from successful payloads, which are always JSON too.
   *
   * <p>This is for arguments the framework accepted but the tool rejects — a month out of range, a
   * username that resolves to nothing. Arguments the framework itself cannot bind (a missing
   * required property, a non-numeric integer) never reach the tool: they surface as a JSON-RPC
   * error instead, which is the protocol's own channel for them.
   */
  static String error(String message) {
    return write(Map.of("error", message));
  }
}
