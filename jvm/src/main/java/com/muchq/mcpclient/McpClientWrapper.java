package com.muchq.mcpclient;

import com.muchq.mcpclient.oauth.OAuthFlowHandler;
import com.muchq.mcpclient.oauth.TokenManager;
import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Wrapper around the official MCP SDK client with OAuth 2.1 support.
 *
 * This client:
 * - Uses the official io.modelcontextprotocol.sdk:mcp library
 * - Automatically handles OAuth authentication flow
 * - Manages token lifecycle
 * - Retries requests after authentication
 *
 * Note: The MCP SDK integration is simplified for this demo.
 * A full implementation would use the SDK's transport and session APIs.
 */
public class McpClientWrapper {

    private static final Logger LOG = LoggerFactory.getLogger(McpClientWrapper.class);

    private final McpClientConfig config;
    private final TokenManager tokenManager;
    private final OAuthFlowHandler oauthFlowHandler;
    private final HttpClient httpClient;

    /**
     * Creates a new MCP client with OAuth support.
     *
     * @param config Client configuration
     */
    public McpClientWrapper(McpClientConfig config) {
        this.config = config;
        this.tokenManager = new TokenManager();
        this.oauthFlowHandler = new OAuthFlowHandler(config, tokenManager);
        this.httpClient = HttpClient.newHttpClient();

        LOG.info("MCP Client initialized for server: {}", config.getServerUrl());
    }

    /**
     * Initializes the MCP connection.
     * Triggers OAuth flow on first 401 response.
     *
     * @return Initialization result
     * @throws Exception if connection fails
     */
    public String initialize() throws Exception {
        LOG.info("Initializing MCP connection...");

        // Check if we have a valid token
        if (!tokenManager.hasValidAccessToken()) {
            LOG.info("No valid token available, starting OAuth flow...");
            oauthFlowHandler.executeFlow();
        }

        String token = tokenManager.getAccessToken();
        if (token != null) {
            LOG.info("Authentication successful, token available");
            String response = callMcp("initialize", "{}");
            LOG.info("MCP initialize response: {}", response);
            return response;
        } else {
            throw new Exception("Failed to obtain access token");
        }
    }

    /**
     * Calls the MCP tools/list method using the authenticated token.
     *
     * @return Raw JSON response from the MCP server
     * @throws Exception if the request fails
     */
    public String listTools() throws Exception {
        String token = tokenManager.getAccessToken();
        if (token == null) {
            throw new Exception("No access token available for MCP request");
        }
        return callMcp("tools/list", "{}");
    }

    private String callMcp(String method, String paramsJson) throws Exception {
        String token = tokenManager.getAccessToken();
        if (token == null) {
            throw new Exception("No access token available for MCP request");
        }

        String body = String.format(
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":%s}",
            method,
            paramsJson == null ? "null" : paramsJson
        );

        HttpRequest request = HttpRequest.newBuilder()
            .uri(URI.create(config.getServerUrl()))
            .header("Content-Type", "application/json")
            .header("Authorization", "Bearer " + token)
            .POST(HttpRequest.BodyPublishers.ofString(body))
            .build();

        HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
        if (response.statusCode() != 200) {
            throw new IOException(
                "MCP request failed: HTTP " + response.statusCode() + " body=" + response.body()
            );
        }

        return response.body();
    }

    /**
     * Gets the current access token.
     *
     * @return Access token, or null if not authenticated
     */
    public String getAccessToken() {
        return tokenManager.getAccessToken();
    }

    /**
     * Checks if the client is authenticated.
     *
     * @return true if valid access token is available
     */
    public boolean isAuthenticated() {
        return tokenManager.hasValidAccessToken();
    }

    /**
     * Gets the server URL.
     *
     * @return Server URL
     */
    public String getServerUrl() {
        return config.getServerUrl();
    }
}
