package com.muchq.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.Map;
import org.junit.Test;

public class AddToolTest {
    private final AddTool tool = new AddTool();

    @Test
    public void testGetName() {
        assertThat(tool.getName()).isEqualTo("add");
    }

    @Test
    public void testGetDescription() {
        assertThat(tool.getDescription()).isEqualTo("Adds two numbers together");
    }

    @Test
    public void testGetInputSchema() {
        Map<String, Object> schema = tool.getInputSchema();
        assertThat(schema).containsKey("type");
        assertThat(schema).containsKey("properties");
        assertThat(schema).containsKey("required");
    }

    @Test
    public void testExecuteWithIntegers() {
        Map<String, Object> arguments = Map.of("a", 5, "b", 3);
        String result = tool.execute(arguments);
        assertThat(result).isEqualTo("8.0");
    }

    @Test
    public void testExecuteWithDoubles() {
        Map<String, Object> arguments = Map.of("a", 5.5, "b", 3.2);
        String result = tool.execute(arguments);
        assertThat(result).isEqualTo("8.7");
    }

    @Test
    public void testExecuteWithNegativeNumbers() {
        Map<String, Object> arguments = Map.of("a", -5, "b", 3);
        String result = tool.execute(arguments);
        assertThat(result).isEqualTo("-2.0");
    }

    @Test
    public void testExecuteWithZero() {
        Map<String, Object> arguments = Map.of("a", 0, "b", 0);
        String result = tool.execute(arguments);
        assertThat(result).isEqualTo("0.0");
    }
}
