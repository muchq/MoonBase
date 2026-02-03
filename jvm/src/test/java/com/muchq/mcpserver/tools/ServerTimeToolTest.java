package com.muchq.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;

import java.time.Clock;
import java.time.Instant;
import java.time.ZoneId;
import java.util.Map;
import org.junit.Test;

public class ServerTimeToolTest {
  private static final Instant FIXED_INSTANT = Instant.parse("2024-01-15T10:30:45.123Z");
  private static final Clock FIXED_CLOCK = Clock.fixed(FIXED_INSTANT, ZoneId.of("UTC"));

  private final ServerTimeTool tool = new ServerTimeTool(FIXED_CLOCK);

  @Test
  public void testGetName() {
    assertThat(tool.getName()).isEqualTo("server_time");
  }

  @Test
  public void testGetDescription() {
    assertThat(tool.getDescription())
        .isEqualTo("Returns the current timestamp according to the server's system clock");
  }

  @Test
  public void testGetInputSchema() {
    Map<String, Object> schema = tool.getInputSchema();
    assertThat(schema).containsKey("type");
    assertThat(schema).containsKey("properties");
    Map<String, Object> properties = (Map<String, Object>) schema.get("properties");
    assertThat(properties).isEmpty();
  }

  @Test
  public void testExecuteReturnsExpectedTimestamp() {
    Map<String, Object> arguments = Map.of();
    String result = tool.execute(arguments);
    assertThat(result).isEqualTo(String.valueOf(FIXED_INSTANT.toEpochMilli()));
  }

  @Test
  public void testExecuteReturnsConsistentValue() {
    Map<String, Object> arguments = Map.of();
    String result1 = tool.execute(arguments);
    String result2 = tool.execute(arguments);
    assertThat(result1).isEqualTo(result2);
  }

  @Test
  public void testExecuteWithSystemClock() {
    ServerTimeTool systemTool = new ServerTimeTool(Clock.systemUTC());
    Map<String, Object> arguments = Map.of();
    String result = systemTool.execute(arguments);
    long timestamp = Long.parseLong(result);
    long now = System.currentTimeMillis();
    assertThat(timestamp).isBetween(now - 1000, now + 1000);
  }
}
