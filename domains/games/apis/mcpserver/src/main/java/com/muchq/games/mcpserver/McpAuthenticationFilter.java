package com.muchq.games.mcpserver;

import io.micronaut.context.annotation.Requires;
import io.micronaut.context.annotation.Value;
import io.micronaut.http.HttpRequest;
import io.micronaut.http.HttpResponse;
import io.micronaut.http.HttpStatus;
import io.micronaut.http.MediaType;
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

/**
 * Bearer-token gate on the MCP endpoint, active only when {@code mcp.auth.token} is set to a
 * non-empty value.
 *
 * <p>The condition must mean "the token holds something usable", and the obvious spellings do not.
 * {@code notEquals = ""} inverts between Micronaut 4 and 5, which disagree about whether the empty
 * placeholder in {@code mcp.auth.token: ${MCP_AUTH_TOKEN:}} is present-and-empty or absent. {@code
 * pattern = ".+"} is worse and in the dangerous direction: {@code String.matches} anchors the whole
 * value and {@code .} excludes line terminators, so a token read from a file-backed secret — a
 * Kubernetes secret, {@code $(cat token)} — arrives as {@code "s3cret\n"}, fails the match, and the
 * bean is never created. The endpoint would serve anonymous {@code tools/call} while the operator
 * reads a configured token, with a missing log line as the only signal. It also admits {@code " "},
 * gating the endpoint behind a secret nobody can send. {@code (?s).*\S.*} is the honest test: at
 * least one non-whitespace character, any character class.
 *
 * <p>The token is stripped for the same reason: a trailing newline that survived into the
 * comparison would reject every correct request.
 *
 * <p>With no token this bean is never created and the endpoint is open, which is what the
 * deployment runs — Compose sets no such variable. {@code McpAuthenticationTest} pins both halves,
 * and the whitespace cases, so "open" stays a decision rather than an accident.
 *
 * <p>The 401 body is written as a JSON-RPC error object so a client that only knows how to parse
 * MCP responses gets a message it can surface. It is serialized by hand rather than handed to the
 * container mapper because that mapper omits nulls, which would drop the {@code id} member that
 * JSON-RPC requires to be present.
 *
 * <p>The route pattern reads the same property the MCP controller is mounted on rather than
 * repeating the literal path, so moving the endpoint cannot leave the gate behind on the old one.
 */
@Filter("${micronaut.mcp.server.endpoint:/mcp}")
@Requires(property = "mcp.auth.token", pattern = "(?s).*\\S.*")
public class McpAuthenticationFilter implements HttpServerFilter {
  private static final Logger LOG = LoggerFactory.getLogger(McpAuthenticationFilter.class);
  private static final String BEARER = "Bearer ";
  private static final int JSON_RPC_UNAUTHORIZED = -32000;

  private final String requiredToken;

  @Inject
  public McpAuthenticationFilter(@Value("${mcp.auth.token}") String requiredToken) {
    this.requiredToken = requiredToken.strip();
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

  /**
   * The id member is null because the request was rejected before its body was read, and JSON-RPC
   * requires it present anyway. Handing a map to the container mapper would drop it — that mapper
   * omits nulls — so the envelope is written literally. Every message is a constant in this file,
   * so there is nothing to escape.
   */
  private static Mono<MutableHttpResponse<?>> unauthorized(String message) {
    String body =
        "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":"
            + JSON_RPC_UNAUTHORIZED
            + ",\"message\":\""
            + message
            + "\"}}";
    return Mono.just(
        HttpResponse.status(HttpStatus.UNAUTHORIZED)
            .header("WWW-Authenticate", "Bearer")
            .contentType(MediaType.APPLICATION_JSON)
            .body(body));
  }
}
