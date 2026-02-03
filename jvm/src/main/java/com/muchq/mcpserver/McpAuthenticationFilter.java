package com.muchq.mcpserver;

import com.muchq.mcpserver.dtos.JsonRpcError;
import com.muchq.mcpserver.dtos.JsonRpcResponse;
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
import org.reactivestreams.Publisher;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import reactor.core.publisher.Mono;

@Filter("/mcp/**")
@Requires(property = "mcp.auth.token")
public class McpAuthenticationFilter implements HttpServerFilter {
  private static final Logger LOG = LoggerFactory.getLogger(McpAuthenticationFilter.class);

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
      return Mono.just(
          HttpResponse.status(HttpStatus.UNAUTHORIZED)
              .body(
                  new JsonRpcResponse(
                      "2.0",
                      null,
                      null,
                      new JsonRpcError(-32000, "Missing Authorization header"))));
    }

    if (!authHeader.startsWith("Bearer ")) {
      LOG.warn("Invalid Authorization header format");
      return Mono.just(
          HttpResponse.status(HttpStatus.UNAUTHORIZED)
              .body(
                  new JsonRpcResponse(
                      "2.0",
                      null,
                      null,
                      new JsonRpcError(-32000, "Invalid Authorization header format"))));
    }

    String token = authHeader.substring(7); // Remove "Bearer " prefix

    if (!MessageDigest.isEqual(
        requiredToken.getBytes(StandardCharsets.UTF_8), token.getBytes(StandardCharsets.UTF_8))) {
      LOG.warn("Invalid authentication token");
      return Mono.just(
          HttpResponse.status(HttpStatus.UNAUTHORIZED)
              .body(
                  new JsonRpcResponse(
                      "2.0",
                      null,
                      null,
                      new JsonRpcError(-32000, "Invalid authentication token"))));
    }

    LOG.debug("Authentication successful");
    return chain.proceed(request);
  }
}
