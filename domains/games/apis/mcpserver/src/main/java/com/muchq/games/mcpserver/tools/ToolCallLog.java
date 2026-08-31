package com.muchq.games.mcpserver.tools;

import io.modelcontextprotocol.spec.McpSchema.CallToolResult;
import java.util.function.Supplier;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * One log line per tool call: name, duration, outcome. Every {@code @Tool} method wraps its body in
 * {@link #logged}, which is the only server-side trace a call leaves — micronaut-mcp's dispatch
 * (ToolRegistry) is final with private handlers, so there is no framework seam to hang this on.
 *
 * <p>Outcomes map to levels: a successful result is INFO, a result the tool flagged {@code isError}
 * is WARN, and an exception that escapes the tool is ERROR with the stack — then rethrown, so the
 * framework's JSON-RPC error mapping is unchanged.
 *
 * <p>Arguments are deliberately not logged: usernames and queries are caller data, and the payload
 * already travels in the response.
 *
 * <p>What this cannot see: a call the framework rejects before the method runs — a missing required
 * argument, an unbindable type — is answered on the {@code isError} channel without reaching any
 * tool, and leaves no line here.
 */
final class ToolCallLog {

  private static final Logger LOG = LoggerFactory.getLogger(ToolCallLog.class);

  private ToolCallLog() {}

  static CallToolResult logged(String tool, Supplier<CallToolResult> call) {
    long start = System.nanoTime();
    CallToolResult result;
    try {
      result = call.get();
    } catch (RuntimeException | Error e) {
      LOG.error("tool={} ms={} outcome=threw", tool, elapsedMs(start), e);
      throw e;
    }
    if (Boolean.TRUE.equals(result.isError())) {
      LOG.warn("tool={} ms={} outcome=error", tool, elapsedMs(start));
    } else {
      LOG.info("tool={} ms={} outcome=ok", tool, elapsedMs(start));
    }
    return result;
  }

  private static long elapsedMs(long startNanos) {
    return (System.nanoTime() - startNanos) / 1_000_000;
  }
}
