package com.muchq.mcpclient;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.mcpclient.McpClientConfig.TransportType;
import org.junit.Test;

public class McpClientConfigTest {

    @Test
    public void testBuilderWithRequiredFieldsOnly() {
        McpClientConfig config = McpClientConfig.builder()
            .serverUrl("http://localhost:8080/mcp")
            .build();

        assertThat(config.getServerUrl()).isEqualTo("http://localhost:8080/mcp");
        assertThat(config.getClientName()).isEqualTo("MCP Java Client"); // default
        assertThat(config.getClientVersion()).isEqualTo("1.0.0"); // default
        assertThat(config.getCallbackPort()).isEqualTo(8888); // default
        assertThat(config.getClientId()).isNull();
        assertThat(config.getClientSecret()).isNull();
        assertThat(config.getTransportType()).isEqualTo(TransportType.HTTP); // default
    }

    @Test
    public void testBuilderWithAllFields() {
        McpClientConfig config = McpClientConfig.builder()
            .serverUrl("http://localhost:8080/mcp")
            .clientName("My Client")
            .clientVersion("2.0.0")
            .callbackPort(9999)
            .clientId("client-123")
            .clientSecret("secret-456")
            .transportType(TransportType.SSE)
            .build();

        assertThat(config.getServerUrl()).isEqualTo("http://localhost:8080/mcp");
        assertThat(config.getClientName()).isEqualTo("My Client");
        assertThat(config.getClientVersion()).isEqualTo("2.0.0");
        assertThat(config.getCallbackPort()).isEqualTo(9999);
        assertThat(config.getClientId()).isEqualTo("client-123");
        assertThat(config.getClientSecret()).isEqualTo("secret-456");
        assertThat(config.getTransportType()).isEqualTo(TransportType.SSE);
    }

    @Test
    public void testBuilderRejectsNullServerUrl() {
        assertThatThrownBy(() -> McpClientConfig.builder().build())
            .isInstanceOf(IllegalArgumentException.class)
            .hasMessageContaining("serverUrl is required");
    }

    @Test
    public void testBuilderRejectsEmptyServerUrl() {
        assertThatThrownBy(() -> McpClientConfig.builder().serverUrl("").build())
            .isInstanceOf(IllegalArgumentException.class)
            .hasMessageContaining("serverUrl is required");
    }

    @Test
    public void testBuilderWithHttpsUrl() {
        McpClientConfig config = McpClientConfig.builder()
            .serverUrl("https://api.example.com/mcp")
            .build();

        assertThat(config.getServerUrl()).isEqualTo("https://api.example.com/mcp");
    }

    @Test
    public void testBuilderMethodsReturnBuilder() {
        McpClientConfig.Builder builder = McpClientConfig.builder();

        // Verify fluent API - all methods return the builder
        assertThat(builder.serverUrl("http://localhost"))
            .isSameAs(builder);
        assertThat(builder.clientName("name"))
            .isSameAs(builder);
        assertThat(builder.clientVersion("1.0"))
            .isSameAs(builder);
        assertThat(builder.callbackPort(8888))
            .isSameAs(builder);
        assertThat(builder.clientId("id"))
            .isSameAs(builder);
        assertThat(builder.clientSecret("secret"))
            .isSameAs(builder);
    }

    @Test
    public void testDefaultClientName() {
        McpClientConfig config = McpClientConfig.builder()
            .serverUrl("http://localhost")
            .build();

        assertThat(config.getClientName()).isEqualTo("MCP Java Client");
    }

    @Test
    public void testDefaultClientVersion() {
        McpClientConfig config = McpClientConfig.builder()
            .serverUrl("http://localhost")
            .build();

        assertThat(config.getClientVersion()).isEqualTo("1.0.0");
    }

    @Test
    public void testDefaultCallbackPort() {
        McpClientConfig config = McpClientConfig.builder()
            .serverUrl("http://localhost")
            .build();

        assertThat(config.getCallbackPort()).isEqualTo(8888);
    }

    @Test
    public void testClientIdAndSecretCanBeSetIndependently() {
        // Test with only clientId (no secret)
        McpClientConfig configWithIdOnly = McpClientConfig.builder()
            .serverUrl("http://localhost")
            .clientId("my-client")
            .build();

        assertThat(configWithIdOnly.getClientId()).isEqualTo("my-client");
        assertThat(configWithIdOnly.getClientSecret()).isNull();

        // Test with only clientSecret (no id)
        McpClientConfig configWithSecretOnly = McpClientConfig.builder()
            .serverUrl("http://localhost")
            .clientSecret("my-secret")
            .build();

        assertThat(configWithSecretOnly.getClientId()).isNull();
        assertThat(configWithSecretOnly.getClientSecret()).isEqualTo("my-secret");
    }

    @Test
    public void testDefaultTransportTypeIsHttp() {
        McpClientConfig config = McpClientConfig.builder()
            .serverUrl("http://localhost")
            .build();

        assertThat(config.getTransportType()).isEqualTo(TransportType.HTTP);
    }

    @Test
    public void testTransportTypeCanBeSetToSse() {
        McpClientConfig config = McpClientConfig.builder()
            .serverUrl("http://localhost")
            .transportType(TransportType.SSE)
            .build();

        assertThat(config.getTransportType()).isEqualTo(TransportType.SSE);
    }

    @Test
    public void testTransportTypeCanBeSetToHttp() {
        McpClientConfig config = McpClientConfig.builder()
            .serverUrl("http://localhost")
            .transportType(TransportType.HTTP)
            .build();

        assertThat(config.getTransportType()).isEqualTo(TransportType.HTTP);
    }

    @Test
    public void testTransportTypeBuilderReturnsSameBuilder() {
        McpClientConfig.Builder builder = McpClientConfig.builder();
        assertThat(builder.transportType(TransportType.SSE)).isSameAs(builder);
    }
}
