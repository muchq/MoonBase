package com.muchq.mcpserver.tools;

import java.time.Clock;
import java.time.Instant;
import java.util.Map;

public class ServerTimeTool implements McpTool {
    private final Clock clock;

    public ServerTimeTool(Clock clock) {
        this.clock = clock;
    }

    @Override
    public String getName() {
        return "server_time";
    }

    @Override
    public String getDescription() {
        return "Returns the current timestamp according to the server's system clock";
    }

    @Override
    public Map<String, Object> getInputSchema() {
        return Map.of("type", "object", "properties", Map.of());
    }

    @Override
    public String execute(Map<String, Object> arguments) {
        return String.valueOf(Instant.now(clock).toEpochMilli());
    }
}
