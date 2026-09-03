package com.muchq.games.mcpserver.tools;

import io.modelcontextprotocol.spec.McpSchema.CallToolResult;
import java.util.function.Supplier;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.slf4j.spi.LoggingEventBuilder;

/**
 * One log line per tool call, message {@code tool_call} with key-value pairs {@code tool}, {@code
 * ms}, and {@code outcome}, so the line aggregates without parsing its text. Every {@code @Tool}
 * method wraps its body in {@link #logged}, which is the only server-side trace a call leaves —
 * micronaut-mcp's dispatch (ToolRegistry) is final with private handlers, so there is no framework
 * seam to hang this on.
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

  static final String MESSAGE = "tool_call";

  static CallToolResult logged(String tool, Supplier<CallToolResult> call) {
    long start = System.nanoTime();
    CallToolResult result;
    try {
      result = call.get();
    } catch (RuntimeException | Error e) {
      line(LOG.atError().setCause(e), tool, elapsedMs(start), "threw");
      throw e;
    }
    if (Boolean.TRUE.equals(result.isError())) {
      line(LOG.atWarn(), tool, elapsedMs(start), "error");
    } else {
      line(LOG.atInfo(), tool, elapsedMs(start), "ok");
    }
    return result;
  }

  private static void line(LoggingEventBuilder builder, String tool, long ms, String outcome) {
    builder
        .addKeyValue("tool", tool)
        .addKeyValue("ms", ms)
        .addKeyValue("outcome", outcome)
        .log(MESSAGE);
  }

  private static long elapsedMs(long startNanos) {
    return (System.nanoTime() - startNanos) / 1_000_000;
  }
}
