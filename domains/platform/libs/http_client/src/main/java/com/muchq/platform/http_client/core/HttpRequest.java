package com.muchq.platform.http_client.core;

import static java.nio.charset.StandardCharsets.UTF_8;

import java.net.URI;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import org.jspecify.annotations.Nullable;

public class HttpRequest {
  private static final String ACCEPT = "Accept";
  private static final String CONTENT_TYPE = "Content-Type";

  public enum Method {
    GET(false),
    POST(true),
    PUT(true),
    DELETE(true),
    PATCH(true),
    HEAD(false);

    private final boolean allowsBody;

    private Method(boolean allowsBody) {
      this.allowsBody = allowsBody;
    }

    public boolean allowsBody() {
      return allowsBody;
    }
  }

  public enum ContentType {
    TEXT("text/plain; charset=UTF-8"),
    JSON("application/json"),
    XML("text/xml"),
    PROTOBUF("application/x-protobuf"),
    FORM("application/x-www-form-urlencoded"),
    CSV("text/csv; charset=UTF-8"),
    OCTET_STREAM("application/octet-stream");

    private final String headerValue;

    ContentType(String headerValue) {
      this.headerValue = headerValue;
    }

    public String getHeaderValue() {
      return headerValue;
    }
  }

  private final Method method;
  private final URI url;
  private final List<Header> headers;
  private final byte @Nullable [] body;
  private final @Nullable Duration timeout;
  private final @Nullable Duration bodyReadTimeout;

  private HttpRequest(
      Method method,
      URI url,
      List<Header> headers,
      byte @Nullable [] body,
      @Nullable Duration timeout,
      @Nullable Duration bodyReadTimeout) {
    this.timeout = timeout;
    this.bodyReadTimeout = bodyReadTimeout;
    this.method = Objects.requireNonNull(method);
    this.url = Objects.requireNonNull(url);
    this.headers = Objects.requireNonNull(headers);
    this.body = body;
  }

  public static Builder newBuilder() {
    return new Builder();
  }

  public Method getMethod() {
    return method;
  }

  public URI getUrl() {
    return url;
  }

  public List<Header> getHeaders() {
    return headers;
  }

  public byte @Nullable [] getBody() {
    return body;
  }

  /**
   * How long to wait for the <em>response head</em>, or empty for no deadline.
   *
   * <p>Read the name literally: this expires while awaiting the status line and headers, and stops
   * there. The body is streamed afterwards and is <b>not</b> covered — a peer that sends a complete
   * head, promises a Content-Length it never delivers, and goes quiet will still park a caller
   * indefinitely in {@code getAsBytes()} (#1336).
   *
   * <p>So this bounds an unresponsive peer, not a slow or malicious body. A caller that needs the
   * whole exchange bounded has to wrap it — {@code OneD4Client} uses a Failsafe {@code Timeout}
   * with {@code withInterrupt()} for exactly that. {@code Jdk11HttpClientTimeoutTest} pins both
   * halves.
   */
  /**
   * How long the response body may take to read in full, or empty for no deadline.
   *
   * <p>The other half of {@link #getResponseHeadersTimeout()}, and the one that matters against a
   * peer which answers and then stalls: the head arrived, so the request deadline is satisfied and
   * the caller is parked in the body read instead.
   *
   * <p>Bounds the whole body rather than each read — a peer dribbling one byte per second would
   * satisfy any per-read deadline forever. Streaming is preserved: the body is not buffered to
   * apply this.
   */
  public Optional<Duration> getBodyReadTimeout() {
    return Optional.ofNullable(bodyReadTimeout);
  }

  public Optional<Duration> getResponseHeadersTimeout() {
    return Optional.ofNullable(timeout);
  }

  public static class Builder {
    private @Nullable String url = null;
    private Method method = Method.GET;
    private final List<Header> headers = new ArrayList<>();
    private byte @Nullable [] body = null;
    private ContentType contentType = ContentType.JSON;
    private ContentType accept = ContentType.JSON;
    private @Nullable Duration timeout = null;
    private @Nullable Duration bodyReadTimeout = null;

    private Builder() {}

    public Builder setUrl(String url) {
      this.url = Objects.requireNonNull(url);
      return this;
    }

    public Builder setMethod(Method method) {
      this.method = Objects.requireNonNull(method);
      return this;
    }

    public Builder addHeader(String name, String value) {
      headers.add(new Header(name, value));
      return this;
    }

    public Builder setBody(String body) {
      return setBody(Objects.requireNonNull(body).getBytes(UTF_8));
    }

    public Builder setBody(byte[] body) {
      this.body = Objects.requireNonNull(body);
      return this;
    }

    public Builder setContentType(ContentType contentType) {
      this.contentType = Objects.requireNonNull(contentType);
      return this;
    }

    public Builder setAccept(ContentType accept) {
      this.accept = Objects.requireNonNull(accept);
      return this;
    }

    /**
     * Deadline for receiving the response head. Does not bound the body read — see {@link
     * HttpRequest#getResponseHeadersTimeout()}.
     */
    public Builder setResponseHeadersTimeout(Duration timeout) {
      this.timeout = Objects.requireNonNull(timeout);
      return this;
    }

    /**
     * Deadline for reading the response body in full. See {@link HttpRequest#getBodyReadTimeout()}.
     */
    public Builder setBodyReadTimeout(Duration bodyReadTimeout) {
      this.bodyReadTimeout = Objects.requireNonNull(bodyReadTimeout);
      return this;
    }

    /** Sets both deadlines to the same value: the common case is "bound the whole exchange". */
    public Builder setTimeouts(Duration timeout) {
      return setResponseHeadersTimeout(timeout).setBodyReadTimeout(timeout);
    }

    public HttpRequest build() {
      URI url = buildUrl();
      List<Header> headers = buildHeaders();
      validateBodyState();

      return new HttpRequest(method, url, headers, body, timeout, bodyReadTimeout);
    }

    private URI buildUrl() {
      Objects.requireNonNull(url, "URL is not set");
      return URI.create(url);
    }

    private void validateBodyState() {
      if (body == null) {
        return;
      }

      if (!method.allowsBody) {
        throw new IllegalStateException("Cannot set body with method " + method);
      }
    }

    private List<Header> buildHeaders() {
      if (contentType != null && !headerPresent(CONTENT_TYPE)) {
        headers.add(new Header(CONTENT_TYPE, contentType.getHeaderValue()));
      }
      if (accept != null && !headerPresent(ACCEPT)) {
        headers.add(new Header(ACCEPT, accept.getHeaderValue()));
      }

      return List.copyOf(headers);
    }

    private boolean headerPresent(String headerName) {
      return headers.stream().anyMatch(header -> header.getName().equalsIgnoreCase(headerName));
    }
  }
}
