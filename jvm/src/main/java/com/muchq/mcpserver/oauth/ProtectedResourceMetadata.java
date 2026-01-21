package com.muchq.mcpserver.oauth;

import com.fasterxml.jackson.annotation.JsonProperty;
import io.micronaut.core.annotation.Introspected;
import java.util.List;

/**
 * RFC 9728 - OAuth 2.0 Protected Resource Metadata
 *
 * This metadata document allows clients to discover:
 * 1. The resource identifier (canonical URI for this server)
 * 2. The authorization server(s) that can issue tokens for this resource
 * 3. Supported bearer token methods (header, query, body)
 *
 * Published at: GET /.well-known/oauth-protected-resource
 *
 * @param resource The resource identifier (RFC 8707) - the canonical URI for this MCP server
 * @param authorizationServers List of authorization server URLs that can issue tokens for this resource
 * @param bearerMethodsSupported List of methods for sending bearer tokens (typically ["header"])
 *
 * @see <a href="https://datatracker.ietf.org/doc/html/rfc9728">RFC 9728</a>
 */
@Introspected
public record ProtectedResourceMetadata(
    @JsonProperty("resource")
    String resource,

    @JsonProperty("authorization_servers")
    List<String> authorizationServers,

    @JsonProperty("bearer_methods_supported")
    List<String> bearerMethodsSupported
) {
}
