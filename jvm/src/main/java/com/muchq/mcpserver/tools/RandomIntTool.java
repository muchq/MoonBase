package com.muchq.mcpserver.tools;

import java.util.List;
import java.util.Map;
import java.util.Random;

public class RandomIntTool implements McpTool {
    @Override
    public String getName() {
        return "random";
    }

    @Override
    public String getDescription() {
        return "Generates a random number between min and max (inclusive)";
    }

    @Override
    public Map<String, Object> getInputSchema() {
        return Map.of(
                "type", "object",
                "properties", Map.of(
                                "min", Map.of("type", "integer", "description", "Minimum value"),
                                "max", Map.of("type", "integer", "description", "Maximum value")),
                "required", List.of("min", "max"));
    }

    @Override
    public String execute(Map<String, Object> arguments) {
        int min = ((Number) arguments.get("min")).intValue();
        int max = ((Number) arguments.get("max")).intValue();
        Random random = new Random();
        int result = random.nextInt(max - min + 1) + min;
        return String.valueOf(result);
    }
}
