package com.muchq.games.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;

import io.modelcontextprotocol.spec.McpSchema.CallToolResult;
import io.modelcontextprotocol.spec.McpSchema.TextContent;

/**
 * Reads a tool's payload back out of its {@link CallToolResult}.
 *
 * <p>These suites call the tools directly rather than over the protocol, so before #1331 they
 * received the payload as a plain {@code String}. They still assert on the same text; this only
 * unwraps the envelope it now arrives in.
 *
 * <p>{@link #payloadOf} asserts the success flag on the way past, and {@link #errorPayloadOf}
 * asserts the failure flag, so every existing call site now pins {@code isError} as well as the
 * text — which is the half that was wrong, and the half a test asserting only the body would keep
 * missing.
 */
final class ToolResultText {

  private ToolResultText() {}

  /** The payload of a call that must have succeeded. */
  static String payloadOf(CallToolResult result) {
    assertThat(result.isError())
        .as("this call was expected to succeed, and isError says otherwise: %s", textOf(result))
        .isFalse();
    return textOf(result);
  }

  /** The payload of a call that must have been rejected. */
  static String errorPayloadOf(CallToolResult result) {
    assertThat(result.isError())
        .as("a rejection has to travel on MCP's isError channel, not only in the body (#1331)")
        .isTrue();
    return textOf(result);
  }

  /** The text without any claim about which channel it came on. */
  static String textOf(CallToolResult result) {
    assertThat(result.content()).as("a tool result carries exactly one text content").hasSize(1);
    assertThat(result.content().get(0)).isInstanceOf(TextContent.class);
    return ((TextContent) result.content().get(0)).text();
  }
}
