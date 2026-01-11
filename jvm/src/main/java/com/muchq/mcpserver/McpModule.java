package com.muchq.mcpserver;

import com.muchq.mcpserver.tools.AddTool;
import com.muchq.mcpserver.tools.EchoTool;
import com.muchq.mcpserver.tools.McpTool;
import com.muchq.mcpserver.tools.RandomIntTool;
import com.muchq.mcpserver.tools.ServerTimeTool;
import com.muchq.mcpserver.tools.ToolRegistry;
import io.micronaut.context.annotation.Context;
import io.micronaut.context.annotation.Factory;

import java.time.Clock;
import java.util.List;

@Factory
public class McpModule {

    @Context
    public Clock clock() {
        return Clock.systemUTC();
    }

    @Context
    public List<McpTool> mcpTools(Clock clock) {
        return List.of(
                new AddTool(),
                new EchoTool(),
                new RandomIntTool(),
                new ServerTimeTool(clock));
    }

    @Context
    public ToolRegistry toolRegistry(List<McpTool> tools) {
        return new ToolRegistry(tools);
    }
}
