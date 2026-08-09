package com.muchq.games.mcpserver.tools;

import io.micronaut.mcp.annotations.Tool;
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
  public String serverTime() {
    return String.valueOf(Instant.now(clock).toEpochMilli());
  }
}
