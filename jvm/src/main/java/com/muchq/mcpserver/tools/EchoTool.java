package com.muchq.mcpserver.tools;

import java.util.List;
import java.util.Map;

@Tool
public class EchoTool implements McpTool {
    @Override
    public String getName() {
        return "echo";
    }

    @Override
    public String getDescription() {
        return "Echoes back the provided message";
    }

    @Override
    public Map<String, Object> getInputSchema() {
        return Map.of(
                "type", "object",
                "properties", Map.of("message", Map.of("type", "string", "description", "The message to echo")),
                "required", List.of("message")
        );
    }

    public String execute(Map<String, Object> arguments) {
        String message = (String) arguments.get("message");
        return "Echo: " + message;
    }
}
