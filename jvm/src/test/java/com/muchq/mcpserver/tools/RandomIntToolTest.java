package com.muchq.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.Map;
import org.junit.Test;

public class RandomIntToolTest {
    private final RandomIntTool tool = new RandomIntTool();

    @Test
    public void testGetName() {
        assertThat(tool.getName()).isEqualTo("random");
    }

    @Test
    public void testGetDescription() {
        assertThat(tool.getDescription()).isEqualTo("Generates a random number between min and max (inclusive)");
    }

    @Test
    public void testGetInputSchema() {
        Map<String, Object> schema = tool.getInputSchema();
        assertThat(schema).containsKey("type");
        assertThat(schema).containsKey("properties");
        assertThat(schema).containsKey("required");
    }

    @Test
    public void testExecuteReturnsValueInRange() {
        Map<String, Object> arguments = Map.of("min", 1, "max", 10);
        String result = tool.execute(arguments);
        int value = Integer.parseInt(result);
        assertThat(value).isBetween(1, 10);
    }

    @Test
    public void testExecuteWithSameMinMax() {
        Map<String, Object> arguments = Map.of("min", 5, "max", 5);
        String result = tool.execute(arguments);
        assertThat(result).isEqualTo("5");
    }

    @Test
    public void testExecuteWithNegativeRange() {
        Map<String, Object> arguments = Map.of("min", -10, "max", -1);
        String result = tool.execute(arguments);
        int value = Integer.parseInt(result);
        assertThat(value).isBetween(-10, -1);
    }

    @Test
    public void testExecuteMultipleTimesForRandomness() {
        Map<String, Object> arguments = Map.of("min", 1, "max", 100);
        String result1 = tool.execute(arguments);
        String result2 = tool.execute(arguments);
        String result3 = tool.execute(arguments);

        int value1 = Integer.parseInt(result1);
        int value2 = Integer.parseInt(result2);
        int value3 = Integer.parseInt(result3);

        assertThat(value1).isBetween(1, 100);
        assertThat(value2).isBetween(1, 100);
        assertThat(value3).isBetween(1, 100);
    }
}
