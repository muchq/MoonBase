package com.muchq.mcpserver.tools;

import com.muchq.mcpserver.dtos.Tool;
import io.micronaut.context.annotation.Context;
import jakarta.inject.Inject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.List;
import java.util.Map;
import java.util.function.Function;
import java.util.stream.Collectors;

@Context
public class ToolRegistry {
  private static final Logger LOG = LoggerFactory.getLogger(ToolRegistry.class);
  private final Map<String, McpTool> toolsByName;

  @Inject
  public ToolRegistry(List<McpTool> tools) {
    this.toolsByName = tools.stream().collect(Collectors.toMap(McpTool::getName, Function.identity()));
    LOG.info("tools: {}", tools);
    for (var tool : tools) {
      LOG.info("registered {} tool", tool.getName());
    }
  }

  public List<Tool> getTools() {
    return toolsByName.values()
            .stream()
            .map(t -> new Tool(t.getName(), t.getDescription(), t.getInputSchema()))
            .toList();
  }

  public String executeTool(String name, Map<String, Object> arguments) {
    LOG.info("Executing tool: {} with arguments: {}", name, arguments);

    McpTool tool = toolsByName.get(name);
    if (tool == null) {
      throw new IllegalArgumentException("Unknown tool: " + name);
    }
    return tool.execute(arguments);
  }
}
