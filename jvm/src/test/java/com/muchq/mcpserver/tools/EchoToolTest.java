package com.muchq.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.Map;
import org.junit.Test;

public class EchoToolTest {
    private final EchoTool tool = new EchoTool();

    @Test
    public void testGetName() {
        assertThat(tool.getName()).isEqualTo("echo");
    }

    @Test
    public void testGetDescription() {
        assertThat(tool.getDescription()).isEqualTo("Echoes back the provided message");
    }

    @Test
    public void testGetInputSchema() {
        Map<String, Object> schema = tool.getInputSchema();
        assertThat(schema).containsKey("type");
        assertThat(schema).containsKey("properties");
        assertThat(schema).containsKey("required");
    }

    @Test
    public void testExecuteWithSimpleMessage() {
        Map<String, Object> arguments = Map.of("message", "Hello, World!");
        String result = tool.execute(arguments);
        assertThat(result).isEqualTo("Echo: Hello, World!");
    }

    @Test
    public void testExecuteWithEmptyMessage() {
        Map<String, Object> arguments = Map.of("message", "");
        String result = tool.execute(arguments);
        assertThat(result).isEqualTo("Echo: ");
    }

    @Test
    public void testExecuteWithSpecialCharacters() {
        Map<String, Object> arguments = Map.of("message", "Test!@#$%^&*()");
        String result = tool.execute(arguments);
        assertThat(result).isEqualTo("Echo: Test!@#$%^&*()");
    }
}
