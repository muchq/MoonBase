package com.muchq.mcpclient.demo;

import com.muchq.mcpclient.McpClientConfig;
import com.muchq.mcpclient.McpClientWrapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Demo application for MCP OAuth 2.1 + PKCE + DCR flow.
 *
 * This demonstrates:
 * 1. Creating an MCP client with OAuth support
 * 2. Automatic OAuth discovery (RFC 9728, RFC 8414)
 * 3. Dynamic Client Registration (RFC 7591)
 * 4. PKCE-protected authorization code flow
 * 5. Token management and usage
 *
 * Prerequisites:
 * - Keycloak running on http://localhost:8180
 * - MCP server running on http://localhost:8080 with OAuth enabled
 *
 * Environment variables:
 * - MCP_SERVER_URL (default: http://localhost:8080/mcp)
 * - MCP_CLIENT_NAME (default: MCP Demo Client)
 * - CALLBACK_PORT (default: 8888)
 */
public class McpClientDemo {

    private static final Logger LOG = LoggerFactory.getLogger(McpClientDemo.class);

    public static void main(String[] args) {
        try {
            LOG.info("=== MCP OAuth Demo Starting ===");

            // Configuration
            String serverUrl = System.getenv().getOrDefault(
                "MCP_SERVER_URL",
                "http://localhost:8080/mcp"
            );
            String clientName = System.getenv().getOrDefault(
                "MCP_CLIENT_NAME",
                "MCP Demo Client"
            );
            String clientId = System.getenv("MCP_CLIENT_ID");
            String clientSecret = System.getenv("MCP_CLIENT_SECRET");
            int callbackPort = Integer.parseInt(
                System.getenv().getOrDefault("CALLBACK_PORT", "8888")
            );

            LOG.info("Server URL: {}", serverUrl);
            LOG.info("Client Name: {}", clientName);
            LOG.info("Callback Port: {}", callbackPort);
            if (clientId != null && !clientId.isEmpty()) {
                LOG.info("Client ID override: {}", clientId);
            }

            // Create client configuration
            McpClientConfig config = McpClientConfig.builder()
                .serverUrl(serverUrl)
                .clientName(clientName)
                .clientId(clientId)
                .clientSecret(clientSecret)
                .callbackPort(callbackPort)
                .build();

            // Create MCP client
            McpClientWrapper client = new McpClientWrapper(config);

            // Initialize connection (triggers OAuth flow on first 401)
            LOG.info("\n=== Step 1: Initializing MCP Connection ===");
            String initResult = client.initialize();
            LOG.info("Initialization result: {}", initResult);

            // Check authentication status
            LOG.info("\n=== Step 2: Checking Authentication ===");
            if (client.isAuthenticated()) {
                LOG.info("Client is authenticated!");
                LOG.info("Access token available: {}", client.getAccessToken().substring(0, 20) + "...");
            } else {
                LOG.error("Client is NOT authenticated!");
                System.exit(1);
            }

            // Make real MCP requests to verify authenticated access
            LOG.info("\n=== Step 3: Calling MCP tools/list ===");
            String toolsResponse = client.listTools();
            LOG.info("tools/list response: {}", toolsResponse);

            LOG.info("\n=== Demo Completed Successfully! ===");
            LOG.info("The OAuth 2.1 + PKCE + DCR flow worked correctly.");
            LOG.info("Key accomplishments:");
            LOG.info("  1. Discovered authorization server via RFC 9728");
            LOG.info("  2. Fetched authorization server metadata via RFC 8414");
            LOG.info("  3. Dynamically registered client via RFC 7591");
            LOG.info("  4. Generated PKCE parameters (S256)");
            LOG.info("  5. Opened browser for user authentication");
            LOG.info("  6. Received authorization code via callback");
            LOG.info("  7. Exchanged code for access token (with resource parameter)");
            LOG.info("  8. Token has correct audience claim for MCP server");
            LOG.info("\nYou can now use this pattern to make authenticated MCP requests!");

        } catch (Exception e) {
            LOG.error("Demo failed with error", e);
            System.exit(1);
        }
    }
}
