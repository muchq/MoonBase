package com.muchq.games.mcpserver.tools;

import io.micronaut.mcp.annotations.Tool;
import io.modelcontextprotocol.spec.McpSchema.CallToolResult;
import jakarta.inject.Singleton;
import java.time.Clock;
import java.time.Instant;

@Singleton
public class ServerTimeTool {
  private final Clock clock;

  public ServerTimeTool(Clock clock) {
    this.clock = clock;
  }

  @Tool(
      name = "server_time",
      description = "Returns the current timestamp according to the server's system clock")
  public CallToolResult serverTime() {
    // text rather than ok: the payload is a bare epoch-millis number, not a JSON document, and
    // ok() would render it as one. This tool has nothing to reject, and returns the protocol type
    // only so that every tool in this package speaks it — the alternative leaves a String-returning
    // tool as a template for the next one, which is how the isError gap in #1331 spread to ten.
    return ToolCallLog.logged(
        "server_time", () -> ToolResults.text(String.valueOf(Instant.now(clock).toEpochMilli())));
  }
}
