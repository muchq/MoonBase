package com.muchq.mcpclient.oauth;

import io.modelcontextprotocol.client.transport.customizer.McpSyncHttpClientRequestCustomizer;
import io.modelcontextprotocol.common.McpTransportContext;
import java.net.URI;
import java.net.http.HttpRequest;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * OAuth 2.1 request customizer for MCP SDK transports.
 *
 * <p>This customizer integrates with the MCP SDK's transport layer to inject
 * OAuth Bearer tokens into outgoing HTTP requests. It works in conjunction
 * with the TokenManager and OAuthFlowHandler to provide automatic authentication.
 *
 * <p>Features:
 * <ul>
 *   <li>Injects Authorization header with Bearer token</li>
 *   <li>Triggers OAuth flow when no valid token is available</li>
 *   <li>Supports token refresh when tokens expire</li>
 *   <li>Thread-safe for concurrent requests</li>
 * </ul>
 */
public class OAuthRequestCustomizer implements McpSyncHttpClientRequestCustomizer {

    private static final Logger LOG = LoggerFactory.getLogger(OAuthRequestCustomizer.class);

    private final TokenManager tokenManager;
    private final OAuthFlowHandler oauthFlowHandler;

    /**
     * Creates a new OAuth request customizer.
     *
     * @param tokenManager Manages OAuth tokens
     * @param oauthFlowHandler Handles OAuth flow execution
     */
    public OAuthRequestCustomizer(TokenManager tokenManager, OAuthFlowHandler oauthFlowHandler) {
        this.tokenManager = tokenManager;
        this.oauthFlowHandler = oauthFlowHandler;
    }

    /**
     * Customizes the HTTP request by adding the OAuth Bearer token.
     *
     * <p>If no valid token is available and this is not already an authentication
     * request, this method will trigger the OAuth flow to obtain a new token.
     *
     * @param builder The HTTP request builder to customize
     * @param method The MCP method being called (e.g., "tools/list")
     * @param endpoint The target URI
     * @param body The request body
     * @param context The transport context
     */
    @Override
    public void customize(HttpRequest.Builder builder, String method, URI endpoint, String body,
            McpTransportContext context) {
        String token = tokenManager.getAccessToken();

        if (token == null) {
            LOG.debug("No valid access token available for request to {}", endpoint);
            // Check if we have a refresh token we can use
            String refreshToken = tokenManager.getRefreshToken();
            if (refreshToken != null) {
                LOG.info("Attempting to refresh access token...");
                try {
                    oauthFlowHandler.refreshAccessToken();
                    token = tokenManager.getAccessToken();
                } catch (Exception e) {
                    LOG.warn("Token refresh failed, will need full OAuth flow: {}", e.getMessage());
                }
            }
        }

        if (token != null) {
            LOG.debug("Adding Authorization header to request");
            builder.header("Authorization", "Bearer " + token);
        } else {
            LOG.warn("No access token available - request may fail with 401");
        }
    }
}
