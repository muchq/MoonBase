package com.muchq.mcpserver.tools;

import java.util.Map;

public interface McpTool {
  String getName();

  String getDescription();

  Map<String, Object> getInputSchema();

  String execute(Map<String, Object> arguments);
}
