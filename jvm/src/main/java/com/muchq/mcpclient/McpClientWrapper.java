package com.muchq.mcpclient;

import com.muchq.mcpclient.oauth.OAuthFlowHandler;
import com.muchq.mcpclient.oauth.OAuthRequestCustomizer;
import com.muchq.mcpclient.oauth.TokenManager;
import io.modelcontextprotocol.client.McpClient;
import io.modelcontextprotocol.client.McpSyncClient;
import io.modelcontextprotocol.client.transport.HttpClientSseClientTransport;
import io.modelcontextprotocol.client.transport.HttpClientStreamableHttpTransport;
import io.modelcontextprotocol.spec.McpClientTransport;
import io.modelcontextprotocol.spec.McpSchema;
import io.modelcontextprotocol.spec.McpSchema.CallToolResult;
import io.modelcontextprotocol.spec.McpSchema.GetPromptResult;
import io.modelcontextprotocol.spec.McpSchema.InitializeResult;
import io.modelcontextprotocol.spec.McpSchema.ListPromptsResult;
import io.modelcontextprotocol.spec.McpSchema.ListResourcesResult;
import io.modelcontextprotocol.spec.McpSchema.ListToolsResult;
import io.modelcontextprotocol.spec.McpSchema.ReadResourceResult;
import java.time.Duration;
import java.util.Map;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Full-featured MCP client wrapper with OAuth 2.1 support.
 *
 * <p>This client integrates with the official MCP SDK (io.modelcontextprotocol.sdk:mcp)
 * and provides automatic OAuth authentication through the SDK's transport customization
 * mechanism.
 *
 * <p>Features:
 * <ul>
 *   <li>Full MCP SDK integration with typed API</li>
 *   <li>Automatic OAuth 2.1 + PKCE authentication</li>
 *   <li>Dynamic Client Registration (RFC 7591)</li>
 *   <li>Token lifecycle management with automatic refresh</li>
 *   <li>HTTP (Streamable) or SSE transport options</li>
 *   <li>Configurable request timeouts</li>
 *   <li>Access to tools, resources, and prompts</li>
 * </ul>
 *
 * <p>Usage example:
 * <pre>{@code
 * McpClientConfig config = McpClientConfig.builder()
 *     .serverUrl("http://localhost:8080/mcp")
 *     .clientName("My MCP Client")
 *     .callbackPort(8888)
 *     .transportType(McpClientConfig.TransportType.HTTP)  // or SSE for streaming
 *     .build();
 *
 * McpClientWrapper client = new McpClientWrapper(config);
 * client.initialize();
 *
 * // List available tools
 * ListToolsResult tools = client.listTools();
 *
 * // Call a tool
 * CallToolResult result = client.callTool("calculator", Map.of(
 *     "operation", "add",
 *     "a", 1,
 *     "b", 2
 * ));
 * }</pre>
 */
public class McpClientWrapper {

    private static final Logger LOG = LoggerFactory.getLogger(McpClientWrapper.class);
    private static final Duration DEFAULT_REQUEST_TIMEOUT = Duration.ofSeconds(30);

    private final McpClientConfig config;
    private final TokenManager tokenManager;
    private final OAuthFlowHandler oauthFlowHandler;
    private final Duration requestTimeout;

    private volatile McpSyncClient mcpClient;
    private volatile McpClientTransport transport;
    private volatile boolean initialized = false;

    /**
     * Creates a new MCP client with OAuth support using default timeout.
     *
     * @param config Client configuration
     */
    public McpClientWrapper(McpClientConfig config) {
        this(config, DEFAULT_REQUEST_TIMEOUT);
    }

    /**
     * Creates a new MCP client with OAuth support and custom timeout.
     *
     * @param config Client configuration
     * @param requestTimeout Timeout for MCP requests
     */
    public McpClientWrapper(McpClientConfig config, Duration requestTimeout) {
        this.config = config;
        this.requestTimeout = requestTimeout;
        this.tokenManager = new TokenManager();
        this.oauthFlowHandler = new OAuthFlowHandler(config, tokenManager);

        LOG.info("MCP Client initialized for server: {}", config.getServerUrl());
    }

    /**
     * Initializes the MCP connection.
     *
     * <p>This method:
     * <ol>
     *   <li>Triggers OAuth flow if no valid token is available</li>
     *   <li>Creates the MCP transport with OAuth authentication</li>
     *   <li>Initializes the MCP session</li>
     * </ol>
     *
     * @return The initialization result containing server capabilities
     * @throws Exception if connection or authentication fails
     */
    public InitializeResult initialize() throws Exception {
        LOG.info("Initializing MCP connection...");

        // Ensure we have a valid token before creating the transport
        if (!tokenManager.hasValidAccessToken()) {
            LOG.info("No valid token available, starting OAuth flow...");
            oauthFlowHandler.executeFlow();
        }

        if (!tokenManager.hasValidAccessToken()) {
            throw new McpAuthenticationException("Failed to obtain access token");
        }

        // Create the transport with OAuth customizer
        OAuthRequestCustomizer oauthCustomizer = new OAuthRequestCustomizer(tokenManager, oauthFlowHandler);

        this.transport = createTransport(oauthCustomizer);

        // Create the MCP client
        this.mcpClient = McpClient.sync(transport)
            .requestTimeout(requestTimeout)
            .build();

        // Initialize the MCP session
        InitializeResult result = mcpClient.initialize();
        this.initialized = true;

        LOG.info("MCP connection initialized successfully");
        LOG.info("Server: {} v{}", result.serverInfo().name(), result.serverInfo().version());
        LOG.info("Protocol version: {}", result.protocolVersion());

        if (result.capabilities() != null) {
            LOG.info("Server capabilities: tools={}, resources={}, prompts={}",
                result.capabilities().tools() != null,
                result.capabilities().resources() != null,
                result.capabilities().prompts() != null);
        }

        return result;
    }

    /**
     * Lists all available tools from the MCP server.
     *
     * @return The list of tools with their schemas
     * @throws Exception if the request fails
     * @throws IllegalStateException if client is not initialized
     */
    public ListToolsResult listTools() throws Exception {
        ensureInitialized();
        return withAuthRetry(() -> mcpClient.listTools());
    }

    /**
     * Calls a tool on the MCP server.
     *
     * @param toolName The name of the tool to call
     * @param arguments The arguments to pass to the tool
     * @return The result of the tool call
     * @throws Exception if the request fails
     * @throws IllegalStateException if client is not initialized
     */
    public CallToolResult callTool(String toolName, Map<String, Object> arguments) throws Exception {
        ensureInitialized();
        return withAuthRetry(() -> mcpClient.callTool(new McpSchema.CallToolRequest(toolName, arguments)));
    }

    /**
     * Lists all available resources from the MCP server.
     *
     * @return The list of resources
     * @throws Exception if the request fails
     * @throws IllegalStateException if client is not initialized
     */
    public ListResourcesResult listResources() throws Exception {
        ensureInitialized();
        return withAuthRetry(() -> mcpClient.listResources());
    }

    /**
     * Reads a resource from the MCP server.
     *
     * @param uri The URI of the resource to read
     * @return The resource content
     * @throws Exception if the request fails
     * @throws IllegalStateException if client is not initialized
     */
    public ReadResourceResult readResource(String uri) throws Exception {
        ensureInitialized();
        return withAuthRetry(() -> mcpClient.readResource(new McpSchema.ReadResourceRequest(uri)));
    }

    /**
     * Lists all available prompts from the MCP server.
     *
     * @return The list of prompts
     * @throws Exception if the request fails
     * @throws IllegalStateException if client is not initialized
     */
    public ListPromptsResult listPrompts() throws Exception {
        ensureInitialized();
        return withAuthRetry(() -> mcpClient.listPrompts());
    }

    /**
     * Gets a prompt from the MCP server.
     *
     * @param promptName The name of the prompt
     * @param arguments The arguments to pass to the prompt
     * @return The rendered prompt
     * @throws Exception if the request fails
     * @throws IllegalStateException if client is not initialized
     */
    public GetPromptResult getPrompt(String promptName, Map<String, Object> arguments) throws Exception {
        ensureInitialized();
        return withAuthRetry(() -> mcpClient.getPrompt(new McpSchema.GetPromptRequest(promptName, arguments)));
    }

    /**
     * Sends a ping to the MCP server to verify connectivity.
     *
     * @throws Exception if the ping fails
     * @throws IllegalStateException if client is not initialized
     */
    public void ping() throws Exception {
        ensureInitialized();
        withAuthRetry(() -> {
            mcpClient.ping();
            return null;
        });
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
     * Checks if the client has been initialized.
     *
     * @return true if initialize() has been called successfully
     */
    public boolean isInitialized() {
        return initialized;
    }

    /**
     * Gets the server URL.
     *
     * @return Server URL
     */
    public String getServerUrl() {
        return config.getServerUrl();
    }

    /**
     * Gets the underlying MCP sync client for advanced usage.
     *
     * <p>This provides direct access to the MCP SDK client for operations
     * not exposed through this wrapper.
     *
     * @return The underlying McpSyncClient, or null if not initialized
     */
    public McpSyncClient getMcpClient() {
        return mcpClient;
    }

    /**
     * Closes the MCP connection gracefully.
     *
     * <p>This method should be called when the client is no longer needed
     * to release resources properly.
     */
    public void close() {
        if (mcpClient != null) {
            try {
                mcpClient.closeGracefully();
                LOG.info("MCP client closed gracefully");
            } catch (Exception e) {
                LOG.warn("Error closing MCP client: {}", e.getMessage());
            }
        }
        this.initialized = false;
        this.mcpClient = null;
        this.transport = null;
    }

    /**
     * Forces re-authentication by clearing tokens and executing OAuth flow.
     *
     * @throws Exception if re-authentication fails
     */
    public void reauthenticate() throws Exception {
        LOG.info("Forcing re-authentication...");
        tokenManager.clearTokens();
        oauthFlowHandler.clearCachedState();
        oauthFlowHandler.executeFlow();
    }

    private void ensureInitialized() {
        if (!initialized || mcpClient == null) {
            throw new IllegalStateException("MCP client not initialized. Call initialize() first.");
        }
    }

    /**
     * Executes an operation with automatic token refresh on authentication failure.
     */
    private <T> T withAuthRetry(McpOperation<T> operation) throws Exception {
        try {
            return operation.execute();
        } catch (Exception e) {
            // Check if this is an authentication error
            if (isAuthenticationError(e)) {
                LOG.info("Request failed with authentication error, attempting token refresh...");
                try {
                    oauthFlowHandler.refreshAccessToken();
                    // Retry the operation
                    return operation.execute();
                } catch (Exception refreshException) {
                    LOG.warn("Token refresh failed: {}", refreshException.getMessage());
                    // If refresh fails, try full re-auth
                    LOG.info("Attempting full re-authentication...");
                    reauthenticate();
                    reinitializeTransport();
                    return operation.execute();
                }
            }
            throw e;
        }
    }

    private void reinitializeTransport() throws Exception {
        LOG.info("Reinitializing transport with new credentials...");

        if (mcpClient != null) {
            try {
                mcpClient.closeGracefully();
            } catch (Exception e) {
                LOG.debug("Error closing previous client: {}", e.getMessage());
            }
        }

        OAuthRequestCustomizer oauthCustomizer = new OAuthRequestCustomizer(tokenManager, oauthFlowHandler);

        this.transport = createTransport(oauthCustomizer);

        this.mcpClient = McpClient.sync(transport)
            .requestTimeout(requestTimeout)
            .build();

        mcpClient.initialize();
    }

    private McpClientTransport createTransport(OAuthRequestCustomizer oauthCustomizer) {
        if (config.getTransportType() == McpClientConfig.TransportType.SSE) {
            LOG.info("Using SSE transport");
            return HttpClientSseClientTransport.builder(config.getServerUrl())
                .httpRequestCustomizer(oauthCustomizer)
                .build();
        } else {
            LOG.info("Using HTTP (Streamable) transport");
            return HttpClientStreamableHttpTransport.builder(config.getServerUrl())
                .httpRequestCustomizer(oauthCustomizer)
                .build();
        }
    }

    private boolean isAuthenticationError(Exception e) {
        String message = e.getMessage();
        if (message == null) {
            return false;
        }
        // Check for common authentication error indicators
        // Be specific to avoid false positives (e.g., "token" could appear in other contexts)
        String lowerMessage = message.toLowerCase();
        return message.contains("401") ||
               message.contains("403") ||
               lowerMessage.contains("unauthorized") ||
               lowerMessage.contains("forbidden") ||
               lowerMessage.contains("authentication failed") ||
               lowerMessage.contains("invalid_token") ||
               lowerMessage.contains("token expired") ||
               lowerMessage.contains("access denied");
    }

    @FunctionalInterface
    private interface McpOperation<T> {
        T execute() throws Exception;
    }

    /**
     * Exception indicating an authentication failure.
     */
    public static class McpAuthenticationException extends Exception {
        public McpAuthenticationException(String message) {
            super(message);
        }

        public McpAuthenticationException(String message, Throwable cause) {
            super(message, cause);
        }
    }
}
