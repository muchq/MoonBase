package com.muchq.games.mcpserver.tools;

import io.micronaut.core.annotation.Nullable;
import io.modelcontextprotocol.spec.McpSchema.CallToolResult;
import java.util.Map;
import java.util.Objects;

/**
 * Tool responses, in the protocol's own result type.
 *
 * <p>The reason this exists rather than tools returning {@code String}: MCP has a channel for "the
 * call failed" — {@code isError} on the tool result — and a tool that answers {@code isError:
 * false} is making a positive claim that it succeeded. Before #1331 every rejection made that claim
 * and buried the failure in a JSON string the caller had to parse to notice. Models generally did
 * notice; nothing made them.
 *
 * <p>This does drag {@code mcp-core} into a package that previously knew nothing about the
 * protocol, which was worth keeping until it cost correctness. It is confined to here and to the
 * ten {@code @Tool} signatures — nothing else in the package sees a protocol type — and the
 * framework passes a returned {@link CallToolResult} through untouched, so success payloads are
 * byte-for-byte what they were.
 *
 * <p>{@link ToolJson} still owns rendering. This owns the envelope around it.
 *
 * <p><b>Which half is the contract.</b> {@code isError} is, and a caller should branch on it. The
 * {@code {"error": "..."}} body is a message for whoever reads it, not a schema to match on — it
 * survived the migration so that anything already parsing it kept working, and it is expected to
 * stay, but a caller that depends on its shape is relying on prose. That asymmetry is the point of
 * having a flag at all: the wording of a rejection should be free to improve without breaking
 * anyone. A later change to the body needs no ceremony; a change to the flag's meaning does.
 */
final class ToolResults {

  private ToolResults() {}

  /** A successful call whose payload is rendered as JSON. */
  static CallToolResult ok(Object payload) {
    return text(ToolJson.write(payload));
  }

  /** A successful call whose payload is already a string, and is not JSON. */
  static CallToolResult text(String text) {
    // isError is set explicitly rather than left null: a caller reading the field should find
    // false, not absent, and the framework sets it explicitly on the paths it builds itself.
    return CallToolResult.builder().addTextContent(text).isError(false).build();
  }

  /**
   * The tool ran and refused: a month out of range, a username that resolves to nothing.
   *
   * <p>Keeps the {@code {"error": "..."}} body that callers may already be parsing, and adds the
   * flag that makes parsing unnecessary. Arguments the framework itself cannot bind never reach a
   * tool — those it already reports as errors on its own.
   */
  static CallToolResult error(@Nullable String message) {
    // Map.of rejects a null value, and the message here is usually an exception's — which may be
    // null. Left alone, the one case that most needs the isError channel would throw on its way
    // into it, and the throw becomes a JSON-RPC error instead: the tool's rejection, reported as a
    // protocol failure, which is the shape this class exists to stop.
    return CallToolResult.builder()
        .addTextContent(ToolJson.write(Map.of("error", Objects.toString(message, "unknown error"))))
        .isError(true)
        .build();
  }
}
