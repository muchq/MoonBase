package com.muchq.mcpclient.demo;

import com.muchq.mcpclient.McpClientConfig;
import com.muchq.mcpclient.McpClientWrapper;
import io.modelcontextprotocol.spec.McpSchema.InitializeResult;
import io.modelcontextprotocol.spec.McpSchema.ListPromptsResult;
import io.modelcontextprotocol.spec.McpSchema.ListResourcesResult;
import io.modelcontextprotocol.spec.McpSchema.ListToolsResult;
import io.modelcontextprotocol.spec.McpSchema.Prompt;
import io.modelcontextprotocol.spec.McpSchema.Resource;
import io.modelcontextprotocol.spec.McpSchema.Tool;
import java.time.Duration;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Demo application for MCP OAuth 2.1 + PKCE + DCR flow with full SDK integration.
 *
 * <p>This demonstrates:
 * <ol>
 *   <li>Creating an MCP client with OAuth support</li>
 *   <li>Automatic OAuth discovery (RFC 9728, RFC 8414)</li>
 *   <li>Dynamic Client Registration (RFC 7591)</li>
 *   <li>PKCE-protected authorization code flow</li>
 *   <li>Token management and automatic refresh</li>
 *   <li>Full MCP SDK integration with typed APIs</li>
 * </ol>
 *
 * <p>Prerequisites:
 * <ul>
 *   <li>Keycloak running on http://localhost:8180</li>
 *   <li>MCP server running on http://localhost:8080 with OAuth enabled</li>
 * </ul>
 *
 * <p>Environment variables:
 * <ul>
 *   <li>MCP_SERVER_URL (default: http://localhost:8080/mcp)</li>
 *   <li>MCP_CLIENT_NAME (default: MCP Demo Client)</li>
 *   <li>CALLBACK_PORT (default: 8888)</li>
 *   <li>MCP_CLIENT_ID (optional: pre-registered client ID)</li>
 *   <li>MCP_CLIENT_SECRET (optional: pre-registered client secret)</li>
 * </ul>
 */
public class McpClientDemo {

    private static final Logger LOG = LoggerFactory.getLogger(McpClientDemo.class);

    public static void main(String[] args) {
        McpClientWrapper client = null;
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

            // Create MCP client with custom timeout
            client = new McpClientWrapper(config, Duration.ofSeconds(30));

            // Initialize connection (triggers OAuth flow if needed)
            LOG.info("\n=== Step 1: Initializing MCP Connection ===");
            InitializeResult initResult = client.initialize();
            LOG.info("Server: {} v{}", initResult.serverInfo().name(), initResult.serverInfo().version());
            LOG.info("Protocol version: {}", initResult.protocolVersion());

            // Check authentication status
            LOG.info("\n=== Step 2: Checking Authentication ===");
            if (client.isAuthenticated()) {
                LOG.info("Client is authenticated!");
                String token = client.getAccessToken();
                LOG.info("Access token available: {}...", token.substring(0, Math.min(20, token.length())));
            } else {
                LOG.error("Client is NOT authenticated!");
                System.exit(1);
            }

            // List available tools
            LOG.info("\n=== Step 3: Listing Available Tools ===");
            ListToolsResult toolsResult = client.listTools();
            if (toolsResult.tools() != null && !toolsResult.tools().isEmpty()) {
                LOG.info("Found {} tools:", toolsResult.tools().size());
                for (Tool tool : toolsResult.tools()) {
                    LOG.info("  - {}: {}", tool.name(), tool.description());
                }
            } else {
                LOG.info("No tools available from server");
            }

            // List available resources (if supported)
            LOG.info("\n=== Step 4: Listing Available Resources ===");
            if (initResult.capabilities() != null && initResult.capabilities().resources() != null) {
                ListResourcesResult resourcesResult = client.listResources();
                if (resourcesResult.resources() != null && !resourcesResult.resources().isEmpty()) {
                    LOG.info("Found {} resources:", resourcesResult.resources().size());
                    for (Resource resource : resourcesResult.resources()) {
                        LOG.info("  - {}: {}", resource.uri(), resource.name());
                    }
                } else {
                    LOG.info("No resources available from server");
                }
            } else {
                LOG.info("Server does not support resources capability - skipping");
            }

            // List available prompts (if supported)
            LOG.info("\n=== Step 5: Listing Available Prompts ===");
            if (initResult.capabilities() != null && initResult.capabilities().prompts() != null) {
                ListPromptsResult promptsResult = client.listPrompts();
                if (promptsResult.prompts() != null && !promptsResult.prompts().isEmpty()) {
                    LOG.info("Found {} prompts:", promptsResult.prompts().size());
                    for (Prompt prompt : promptsResult.prompts()) {
                        LOG.info("  - {}: {}", prompt.name(), prompt.description());
                    }
                } else {
                    LOG.info("No prompts available from server");
                }
            } else {
                LOG.info("Server does not support prompts capability - skipping");
            }

            LOG.info("\n=== Demo Completed Successfully! ===");
            LOG.info("The OAuth 2.1 + PKCE + DCR flow with full MCP SDK integration worked correctly.");
            LOG.info("Key accomplishments:");
            LOG.info("  1. Discovered authorization server via RFC 9728");
            LOG.info("  2. Fetched authorization server metadata via RFC 8414");
            LOG.info("  3. Dynamically registered client via RFC 7591");
            LOG.info("  4. Generated PKCE parameters (S256)");
            LOG.info("  5. Opened browser for user authentication");
            LOG.info("  6. Received authorization code via callback");
            LOG.info("  7. Exchanged code for access token (with resource parameter)");
            LOG.info("  8. Successfully used MCP SDK with typed APIs");
            LOG.info("\nYou can now use this pattern to make authenticated MCP requests!");

        } catch (Exception e) {
            LOG.error("Demo failed with error", e);
            System.exit(1);
        } finally {
            // Clean up resources
            if (client != null) {
                client.close();
            }
        }
    }
}
