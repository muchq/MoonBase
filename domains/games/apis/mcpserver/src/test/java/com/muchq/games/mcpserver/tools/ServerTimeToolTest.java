package com.muchq.games.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;

import java.time.Clock;
import java.time.Instant;
import java.time.ZoneId;
import org.junit.jupiter.api.Test;

public class ServerTimeToolTest {
  private static final Instant FIXED_INSTANT = Instant.parse("2024-01-15T10:30:45.123Z");
  private static final Clock FIXED_CLOCK = Clock.fixed(FIXED_INSTANT, ZoneId.of("UTC"));

  private final ServerTimeTool tool = new ServerTimeTool(FIXED_CLOCK);

  @Test
  public void testReturnsExpectedTimestamp() {
    assertThat(tool.serverTime()).isEqualTo(String.valueOf(FIXED_INSTANT.toEpochMilli()));
  }

  @Test
  public void testReturnsConsistentValue() {
    assertThat(tool.serverTime()).isEqualTo(tool.serverTime());
  }

  @Test
  public void testWithSystemClock() {
    ServerTimeTool systemTool = new ServerTimeTool(Clock.systemUTC());
    long timestamp = Long.parseLong(systemTool.serverTime());
    long now = System.currentTimeMillis();
    assertThat(timestamp).isBetween(now - 1000, now + 1000);
  }
}
