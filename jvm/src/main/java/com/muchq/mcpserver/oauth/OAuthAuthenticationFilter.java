package com.muchq.mcpserver.oauth;

import com.muchq.mcpserver.dtos.JsonRpcError;
import com.muchq.mcpserver.dtos.JsonRpcResponse;
import io.micronaut.context.annotation.Requires;
import io.micronaut.http.HttpRequest;
import io.micronaut.http.HttpResponse;
import io.micronaut.http.HttpStatus;
import io.micronaut.http.MutableHttpResponse;
import io.micronaut.http.annotation.Filter;
import io.micronaut.http.filter.HttpServerFilter;
import io.micronaut.http.filter.ServerFilterChain;
import jakarta.inject.Inject;
import org.reactivestreams.Publisher;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import reactor.core.publisher.Mono;

/**
 * OAuth 2.1 authentication filter for MCP endpoints.
 *
 * This filter intercepts all requests to /mcp/** and validates JWT access tokens.
 * When OAuth is disabled, the legacy McpAuthenticationFilter is used instead.
 *
 * Flow:
 * 1. Extract Authorization header
 * 2. Validate Bearer token format
 * 3. Validate JWT signature, expiration, and audience
 * 4. Return 401 with WWW-Authenticate header if validation fails
 * 5. Allow request to proceed if valid
 *
 * WWW-Authenticate header format (RFC 6750):
 * Bearer realm="mcp", error="invalid_token", resource="<metadata_url>"
 */
@Filter("/mcp/**")
@Requires(property = "mcp.oauth.enabled", value = "true")
public class OAuthAuthenticationFilter implements HttpServerFilter {

    private static final Logger LOG = LoggerFactory.getLogger(OAuthAuthenticationFilter.class);

    private final TokenValidator tokenValidator;
    private final OAuthConfig config;

    @Inject
    public OAuthAuthenticationFilter(TokenValidator tokenValidator, OAuthConfig config) {
        this.tokenValidator = tokenValidator;
        this.config = config;
    }

    @Override
    public Publisher<MutableHttpResponse<?>> doFilter(
        HttpRequest<?> request,
        ServerFilterChain chain
    ) {
        LOG.debug("OAuth filter processing request: {} {}", request.getMethod(), request.getPath());

        // Extract Authorization header
        String authHeader = request.getHeaders().get("Authorization");

        if (authHeader == null || authHeader.isEmpty()) {
            LOG.warn("Missing Authorization header");
            return Mono.just(createUnauthorizedResponse("invalid_token", "Missing Authorization header"));
        }

        // Validate Bearer token format
        if (!authHeader.startsWith("Bearer ")) {
            LOG.warn("Invalid Authorization header format: {}", authHeader.substring(0, Math.min(20, authHeader.length())));
            return Mono.just(createUnauthorizedResponse("invalid_token", "Authorization header must use Bearer scheme"));
        }

        // Extract token (remove "Bearer " prefix)
        String token = authHeader.substring(7);

        // Validate token
        TokenValidator.ValidationResult result = tokenValidator.validate(token);

        if (!result.isValid()) {
            LOG.warn("Token validation failed: {}", result.getErrorMessage());
            return Mono.just(createUnauthorizedResponse("invalid_token", result.getErrorMessage()));
        }

        // Token is valid, allow request to proceed
        LOG.debug("Token validated successfully for subject: {}", result.getSubject());
        return Mono.from(chain.proceed(request));
    }

    /**
     * Creates a 401 Unauthorized response with WWW-Authenticate header.
     *
     * The WWW-Authenticate header follows RFC 6750 and RFC 9728:
     * - realm: Identifies the protection space
     * - error: OAuth error code
     * - resource: URL to Protected Resource Metadata (RFC 9728)
     *
     * @param error OAuth error code (e.g., "invalid_token", "invalid_request")
     * @param errorDescription Human-readable error description
     * @return HTTP 401 response with JSON-RPC error body
     */
    private MutableHttpResponse<?> createUnauthorizedResponse(String error, String errorDescription) {
        // Build WWW-Authenticate header per RFC 6750
        String resourceMetadataUrl = config.getResourceUri() + "/.well-known/oauth-protected-resource";
        String wwwAuthenticate = String.format(
            "Bearer realm=\"mcp\", error=\"%s\", error_description=\"%s\", resource=\"%s\"",
            error,
            errorDescription,
            resourceMetadataUrl
        );

        // Create JSON-RPC error response
        JsonRpcResponse jsonRpcError = new JsonRpcResponse(
            "2.0",
            null,
            null,
            new JsonRpcError(-32000, "Unauthorized: " + errorDescription)
        );

        return HttpResponse.status(HttpStatus.UNAUTHORIZED)
            .header("WWW-Authenticate", wwwAuthenticate)
            .body(jsonRpcError);
    }
}
