package com.muchq.mcpclient;

/**
 * Configuration for the MCP OAuth client.
 *
 * This configuration is used to:
 * - Connect to the MCP server
 * - Identify the client during OAuth registration
 * - Configure the local OAuth callback server
 */
public class McpClientConfig {

    private final String serverUrl;
    private final String clientName;
    private final String clientVersion;
    private final int callbackPort;
    private final String clientId;
    private final String clientSecret;

    private McpClientConfig(Builder builder) {
        this.serverUrl = builder.serverUrl;
        this.clientName = builder.clientName;
        this.clientVersion = builder.clientVersion;
        this.callbackPort = builder.callbackPort;
        this.clientId = builder.clientId;
        this.clientSecret = builder.clientSecret;
    }

    public String getServerUrl() {
        return serverUrl;
    }

    public String getClientName() {
        return clientName;
    }

    public String getClientVersion() {
        return clientVersion;
    }

    public int getCallbackPort() {
        return callbackPort;
    }

    public String getClientId() {
        return clientId;
    }

    public String getClientSecret() {
        return clientSecret;
    }

    public static Builder builder() {
        return new Builder();
    }

    public static class Builder {
        private String serverUrl;
        private String clientName = "MCP Java Client";
        private String clientVersion = "1.0.0";
        private int callbackPort = 8888;
        private String clientId;
        private String clientSecret;

        public Builder serverUrl(String serverUrl) {
            this.serverUrl = serverUrl;
            return this;
        }

        public Builder clientName(String clientName) {
            this.clientName = clientName;
            return this;
        }

        public Builder clientVersion(String clientVersion) {
            this.clientVersion = clientVersion;
            return this;
        }

        public Builder callbackPort(int callbackPort) {
            this.callbackPort = callbackPort;
            return this;
        }

        public Builder clientId(String clientId) {
            this.clientId = clientId;
            return this;
        }

        public Builder clientSecret(String clientSecret) {
            this.clientSecret = clientSecret;
            return this;
        }

        public McpClientConfig build() {
            if (serverUrl == null || serverUrl.isEmpty()) {
                throw new IllegalArgumentException("serverUrl is required");
            }
            return new McpClientConfig(this);
        }
    }
}
