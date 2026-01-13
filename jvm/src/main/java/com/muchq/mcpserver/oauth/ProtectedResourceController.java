package com.muchq.mcpserver.oauth;

import io.micronaut.http.annotation.Controller;
import io.micronaut.http.annotation.Get;
import java.util.List;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Controller for serving RFC 9728 Protected Resource Metadata.
 *
 * This endpoint allows MCP clients to discover:
 * 1. The canonical resource URI for this server
 * 2. Which authorization server(s) can issue tokens for this resource
 * 3. How to send bearer tokens (Authorization header)
 *
 * Flow:
 * 1. Client makes unauthenticated MCP request
 * 2. Server returns 401 with WWW-Authenticate header pointing to this endpoint
 * 3. Client fetches this metadata to discover the authorization server
 * 4. Client proceeds with OAuth flow using the discovered authorization server
 *
 * @see <a href="https://datatracker.ietf.org/doc/html/rfc9728">RFC 9728</a>
 */
@Controller("/.well-known")
public class ProtectedResourceController {

    private static final Logger LOG = LoggerFactory.getLogger(ProtectedResourceController.class);

    private final OAuthConfig config;

    public ProtectedResourceController(OAuthConfig config) {
        this.config = config;
    }

    /**
     * Serves Protected Resource Metadata per RFC 9728.
     *
     * This endpoint is accessible without authentication and provides
     * OAuth discovery information to clients.
     *
     * Example response:
     * {
     *   "resource": "http://localhost:8080",
     *   "authorization_servers": ["http://localhost:8180/realms/mcp-demo"],
     *   "bearer_methods_supported": ["header"]
     * }
     *
     * @return Protected Resource Metadata
     */
    @Get("/oauth-protected-resource")
    public ProtectedResourceMetadata getMetadata() {
        LOG.debug("Serving Protected Resource Metadata: resource={}, authz_server={}",
            config.getResourceUri(), config.getAuthorizationServer());

        return new ProtectedResourceMetadata(
            config.getResourceUri(),
            List.of(config.getAuthorizationServer()),
            List.of("header")
        );
    }
}
