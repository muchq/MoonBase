package com.muchq.games.mcpserver;

import io.micronaut.context.annotation.Requires;
import io.micronaut.context.annotation.Value;
import io.micronaut.http.HttpRequest;
import io.micronaut.http.HttpResponse;
import io.micronaut.http.HttpStatus;
import io.micronaut.http.MutableHttpResponse;
import io.micronaut.http.annotation.Filter;
import io.micronaut.http.filter.HttpServerFilter;
import io.micronaut.http.filter.ServerFilterChain;
import jakarta.inject.Inject;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.LinkedHashMap;
import java.util.Map;
import org.reactivestreams.Publisher;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import reactor.core.publisher.Mono;

/**
 * Bearer-token gate on the MCP endpoint, active only when {@code mcp.auth.token} is set to a
 * non-empty value.
 *
 * <p>The shared {@code application.yml} declares {@code mcp.auth.token: ${MCP_AUTH_TOKEN:}}, and
 * with {@code MCP_AUTH_TOKEN} unset that resolves to nothing. The condition is written as a
 * <em>pattern</em> rather than a presence or {@code notEquals} check because Micronaut 4 and 5
 * disagree about which of those an empty placeholder is: on 4 the property was present-and-empty,
 * on 5 it is absent, and {@code notEquals = ""} flips meaning between them — under 5 it admitted
 * the bean and then failed to inject {@code ${mcp.auth.token}}, turning every request into a 500.
 * {@code pattern = ".+"} says the thing actually meant — a token with at least one character — and
 * reads the same on both. With no token this bean is never created and the endpoint is open, which
 * is what the deployment runs: Compose sets no such variable. {@code McpAuthenticationTest} pins
 * both halves so "open" stays a decision rather than an accident.
 *
 * <p>The 401 body is a JSON-RPC error object rather than an empty response: a client that only
 * knows how to parse MCP responses gets a message it can surface instead of a bare status code.
 *
 * <p>The pattern reads the same property the MCP controller is mounted on rather than repeating the
 * literal path, so moving the endpoint cannot leave the gate behind on the old one.
 */
@Filter("${micronaut.mcp.server.endpoint:/mcp}")
@Requires(property = "mcp.auth.token", pattern = ".+")
public class McpAuthenticationFilter implements HttpServerFilter {
  private static final Logger LOG = LoggerFactory.getLogger(McpAuthenticationFilter.class);
  private static final String BEARER = "Bearer ";

  private final String requiredToken;

  @Inject
  public McpAuthenticationFilter(@Value("${mcp.auth.token}") String requiredToken) {
    this.requiredToken = requiredToken;
    LOG.info("MCP authentication enabled");
  }

  @Override
  public Publisher<MutableHttpResponse<?>> doFilter(
      HttpRequest<?> request, ServerFilterChain chain) {
    String authHeader = request.getHeaders().get("Authorization");

    if (authHeader == null) {
      LOG.warn("Missing Authorization header");
      return unauthorized("Missing Authorization header");
    }

    if (!authHeader.startsWith(BEARER)) {
      LOG.warn("Invalid Authorization header format");
      return unauthorized("Invalid Authorization header format");
    }

    String token = authHeader.substring(BEARER.length());

    if (!MessageDigest.isEqual(
        requiredToken.getBytes(StandardCharsets.UTF_8), token.getBytes(StandardCharsets.UTF_8))) {
      LOG.warn("Invalid authentication token");
      return unauthorized("Invalid authentication token");
    }

    LOG.debug("Authentication successful");
    return chain.proceed(request);
  }

  private static Mono<MutableHttpResponse<?>> unauthorized(String message) {
    // LinkedHashMap rather than Map.of: JSON-RPC wants the id member present, and it is null here
    // because the request was rejected before its body was read.
    Map<String, Object> error = new LinkedHashMap<>();
    error.put("code", -32000);
    error.put("message", message);
    Map<String, Object> body = new LinkedHashMap<>();
    body.put("jsonrpc", "2.0");
    body.put("id", null);
    body.put("error", error);
    return Mono.just(
        HttpResponse.status(HttpStatus.UNAUTHORIZED)
            .header("WWW-Authenticate", "Bearer")
            .body(body));
  }
}
