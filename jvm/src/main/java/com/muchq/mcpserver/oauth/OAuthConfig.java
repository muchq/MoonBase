package com.muchq.mcpserver.oauth;

import io.micronaut.context.annotation.ConfigurationProperties;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Configuration for OAuth 2.1 / OpenID Connect integration with Keycloak.
 *
 * These settings are used for:
 * - Publishing Protected Resource Metadata (RFC 9728)
 * - Validating JWT access tokens
 * - Advertising the authorization server
 */
@ConfigurationProperties("mcp.oauth")
public class OAuthConfig {

    private static final Logger LOG = LoggerFactory.getLogger(OAuthConfig.class);

    /**
     * Whether OAuth authentication is enabled.
     * When false, the legacy Bearer token authentication (MCP_AUTH_TOKEN) is used.
     * When true, JWT token validation with Keycloak is used.
     */
    private boolean enabled = true;

    /**
     * The authorization server URL (Keycloak realm).
     * Example: http://localhost:8180/realms/mcp-demo
     *
     * This is advertised in the Protected Resource Metadata (RFC 9728)
     * so clients can discover where to obtain tokens.
     */
    private String authorizationServer = "http://localhost:8180/realms/mcp-demo";

    /**
     * The resource URI for this MCP server.
     * This is the canonical identifier for this server in OAuth terms.
     * Example: http://localhost:8080
     *
     * CRITICAL: This MUST match the audience (aud) claim in JWT tokens.
     * Tokens without this audience will be rejected (RFC 8707).
     */
    private String resourceUri = "http://localhost:8080";

    /**
     * The JWKS (JSON Web Key Set) URI from Keycloak.
     * Used to fetch public keys for validating JWT signatures.
     * Example: http://localhost:8180/realms/mcp-demo/protocol/openid-connect/certs
     */
    private String jwksUri = "http://localhost:8180/realms/mcp-demo/protocol/openid-connect/certs";

    public boolean isEnabled() {
        return enabled;
    }

    public void setEnabled(boolean enabled) {
        this.enabled = enabled;
    }

    public String getAuthorizationServer() {
        LOG.debug("getAuthorizationServer() called, returning: {}", authorizationServer);
        return authorizationServer;
    }

    public void setAuthorizationServer(String authorizationServer) {
        LOG.info("setAuthorizationServer() called with: {}", authorizationServer);
        this.authorizationServer = authorizationServer;
    }

    public String getResourceUri() {
        LOG.debug("getResourceUri() called, returning: {}", resourceUri);
        return resourceUri;
    }

    public void setResourceUri(String resourceUri) {
        LOG.info("setResourceUri() called with: {}", resourceUri);
        this.resourceUri = resourceUri;
    }

    public String getJwksUri() {
        return jwksUri;
    }

    public void setJwksUri(String jwksUri) {
        this.jwksUri = jwksUri;
    }
}
