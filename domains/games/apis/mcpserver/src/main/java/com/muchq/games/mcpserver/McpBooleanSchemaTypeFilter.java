package com.muchq.games.mcpserver;

import io.micronaut.http.HttpResponse;
import io.micronaut.http.MutableHttpResponse;
import io.micronaut.http.annotation.ResponseFilter;
import io.micronaut.http.annotation.ServerFilter;
import io.modelcontextprotocol.spec.McpSchema;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Corrects the JSON Schema type micronaut-mcp emits for boolean tool arguments.
 *
 * <p>micronaut-mcp 0.0.20 writes {@code "type": "bool"} — see {@code
 * io.micronaut.mcp.server.registry.JsonSchemaUtils#TYPE_BOOL}. JSON Schema has no such type; the
 * spelling is {@code "boolean"}. Upstream fixed the constant, but only on the 2.0.x line, which
 * requires Micronaut 5 — this repo is on the Micronaut 4 core BOM, and 0.0.20 is the last
 * micronaut-mcp release built against it.
 *
 * <p>It matters because {@code inputSchema} is not decoration: a client hands it to a model
 * provider, and a provider that validates the schema rejects an unknown {@code type}. Six arguments
 * across three tools are booleans ({@code rated}, {@code include_pgn}, {@code include_tcn}, {@code
 * exclude_bullet}, {@code skip_cache}), so the failure would not be a corner case — it would be
 * most of the roster.
 *
 * <p>This rebuilds the affected records rather than editing the schema maps in place. Those maps
 * are built once at startup and shared by every response, so mutating them would be a write to
 * shared state from request threads for no gain; {@code tools/list} runs once per client session,
 * and copying it is cheap.
 *
 * <p>Delete this the day micronaut-mcp ships the fix on a Micronaut 4-compatible release, or the
 * day this repo moves to Micronaut 5. {@code McpProtocolTest} asserts the served type, so the
 * filter's removal will fail loudly if it is still needed.
 */
@ServerFilter("${micronaut.mcp.server.endpoint:/mcp}")
public class McpBooleanSchemaTypeFilter {

  private static final String KEY_TYPE = "type";
  private static final String WRONG = "bool";
  private static final String CORRECT = "boolean";

  @ResponseFilter
  public void correctBooleanSchemaTypes(MutableHttpResponse<?> response) {
    if (!(response.body() instanceof McpSchema.JSONRPCResponse rpcResponse)
        || !(rpcResponse.result() instanceof McpSchema.ListToolsResult listToolsResult)) {
      return;
    }

    List<McpSchema.Tool> corrected = new ArrayList<>(listToolsResult.tools().size());
    boolean changed = false;
    for (McpSchema.Tool tool : listToolsResult.tools()) {
      McpSchema.Tool fixed = correct(tool);
      changed |= fixed != tool;
      corrected.add(fixed);
    }
    if (!changed) {
      return;
    }

    setBody(
        response,
        new McpSchema.JSONRPCResponse(
            rpcResponse.jsonrpc(),
            rpcResponse.id(),
            new McpSchema.ListToolsResult(
                corrected, listToolsResult.nextCursor(), listToolsResult.meta()),
            rpcResponse.error()));
  }

  /**
   * Returns the tool unchanged when nothing needs correcting, so the common case allocates once.
   */
  private static McpSchema.Tool correct(McpSchema.Tool tool) {
    McpSchema.JsonSchema schema = tool.inputSchema();
    if (schema == null || schema.properties() == null) {
      return tool;
    }

    Map<String, Object> properties = new LinkedHashMap<>(schema.properties());
    boolean changed = false;
    for (Map.Entry<String, Object> property : properties.entrySet()) {
      if (property.getValue() instanceof Map<?, ?> propertySchema
          && WRONG.equals(propertySchema.get(KEY_TYPE))) {
        Map<String, Object> copy = new LinkedHashMap<>();
        propertySchema.forEach((k, v) -> copy.put(String.valueOf(k), v));
        copy.put(KEY_TYPE, CORRECT);
        property.setValue(copy);
        changed = true;
      }
    }
    if (!changed) {
      return tool;
    }

    return new McpSchema.Tool(
        tool.name(),
        tool.title(),
        tool.description(),
        new McpSchema.JsonSchema(
            schema.type(),
            properties,
            schema.required(),
            schema.additionalProperties(),
            schema.defs(),
            schema.definitions()),
        tool.outputSchema(),
        tool.annotations(),
        tool.meta());
  }

  @SuppressWarnings("unchecked")
  private static void setBody(MutableHttpResponse<?> response, Object body) {
    ((MutableHttpResponse<Object>) (HttpResponse<?>) response).body(body);
  }
}
