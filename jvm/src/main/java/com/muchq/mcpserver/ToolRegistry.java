package com.muchq.mcpserver;

import jakarta.inject.Singleton;
import java.time.Instant;
import java.util.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class ToolRegistry {
  private static final Logger LOG = LoggerFactory.getLogger(ToolRegistry.class);
  private final List<Tool> tools;

  public ToolRegistry() {
    this.tools = new ArrayList<>();
    registerDefaultTools();
  }

  private void registerDefaultTools() {
    // Echo tool
    tools.add(
        new Tool(
            "echo",
            "Echoes back the provided message",
            Map.of(
                "type",
                "object",
                "properties",
                Map.of("message", Map.of("type", "string", "description", "The message to echo")),
                "required",
                List.of("message"))));

    // Add numbers tool
    tools.add(
        new Tool(
            "add",
            "Adds two numbers together",
            Map.of(
                "type",
                "object",
                "properties",
                Map.of(
                    "a", Map.of("type", "number", "description", "First number"),
                    "b", Map.of("type", "number", "description", "Second number")),
                "required",
                List.of("a", "b"))));

    // Get timestamp tool
    tools.add(
        new Tool(
            "get_timestamp",
            "Returns the current UTC timestamp",
            Map.of("type", "object", "properties", Map.of())));

    // Random number tool
    tools.add(
        new Tool(
            "random",
            "Generates a random number between min and max (inclusive)",
            Map.of(
                "type",
                "object",
                "properties",
                Map.of(
                    "min", Map.of("type", "integer", "description", "Minimum value"),
                    "max", Map.of("type", "integer", "description", "Maximum value")),
                "required",
                List.of("min", "max"))));
  }

  public List<Tool> getTools() {
    return Collections.unmodifiableList(tools);
  }

  public String executeTool(String name, Map<String, Object> arguments) {
    LOG.info("Executing tool: {} with arguments: {}", name, arguments);

    return switch (name) {
      case "echo" -> executeEcho(arguments);
      case "add" -> executeAdd(arguments);
      case "get_timestamp" -> executeGetTimestamp(arguments);
      case "random" -> executeRandom(arguments);
      default -> throw new IllegalArgumentException("Unknown tool: " + name);
    };
  }

  private String executeEcho(Map<String, Object> arguments) {
    String message = (String) arguments.get("message");
    return "Echo: " + message;
  }

  private String executeAdd(Map<String, Object> arguments) {
    Number a = (Number) arguments.get("a");
    Number b = (Number) arguments.get("b");
    double result = a.doubleValue() + b.doubleValue();
    return String.valueOf(result);
  }

  private String executeGetTimestamp(Map<String, Object> arguments) {
    return Instant.now().toString();
  }

  private String executeRandom(Map<String, Object> arguments) {
    int min = ((Number) arguments.get("min")).intValue();
    int max = ((Number) arguments.get("max")).intValue();
    Random random = new Random();
    int result = random.nextInt(max - min + 1) + min;
    return String.valueOf(result);
  }
}
