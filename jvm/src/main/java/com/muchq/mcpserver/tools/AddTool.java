package com.muchq.mcpserver.tools;

import java.util.List;
import java.util.Map;

@Tool
public class AddTool implements McpTool {
    @Override
    public String getName() {
        return "add";
    }

    @Override
    public String getDescription() {
        return "Adds two numbers together";
    }

    @Override
    public Map<String, Object> getInputSchema() {
        return Map.of(
                "type", "object",
                "properties", Map.of(
                                "a", Map.of("type", "number", "description", "First number"),
                                "b", Map.of("type", "number", "description", "Second number")),

                "required", List.of("a", "b")
        );
    }

    @Override
    public String execute(Map<String, Object> arguments) {
        Number a = (Number) arguments.get("a");
        Number b = (Number) arguments.get("b");
        double result = a.doubleValue() + b.doubleValue();
        return String.valueOf(result);
    }
}
