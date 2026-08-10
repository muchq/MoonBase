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

  private HttpRequest(
      Method method,
      URI url,
      List<Header> headers,
      byte @Nullable [] body,
      @Nullable Duration timeout) {
    this.timeout = timeout;
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
   * Deadline for the whole exchange, or empty for none.
   *
   * <p>Headers and body, genuinely: the client sends asynchronously and waits for the complete
   * response, so this fires whether the peer stalls before answering or halfway through the body.
   * An earlier version of this only covered time-to-headers while claiming otherwise, which is a
   * worse failure than having no deadline — a caller sets one, reads the name, and believes the
   * body read is bounded when it is not (#1336).
   *
   * <p>Worth setting on any call to a peer you do not control. Without it a stalled server parks
   * the calling thread indefinitely, and indefinitely outlasts every retry and budget above it.
   */
  public Optional<Duration> getTimeout() {
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

    /** Deadline for the whole exchange — headers and body. */
    public Builder setTimeout(Duration timeout) {
      this.timeout = Objects.requireNonNull(timeout);
      return this;
    }

    public HttpRequest build() {
      URI url = buildUrl();
      List<Header> headers = buildHeaders();
      validateBodyState();

      return new HttpRequest(method, url, headers, body, timeout);
    }

    /**
     * Parses the URL and refuses one {@code java.net.http} could not send.
     *
     * <p>{@link URI#create} is the more permissive of the two: it accepts a relative path, a URI
     * with no host, and an authority containing an underscore, for which RFC 3986's reg-name rule
     * leaves {@code getHost()} null. The JDK client rejects all three at execute(), where callers
     * are already wrapping failures as a dead peer. Rejecting them here names the URL instead.
     */
    private URI buildUrl() {
      Objects.requireNonNull(url, "URL is not set");
      URI parsed = URI.create(url);
      if (parsed.getHost() == null) {
        throw new IllegalArgumentException("URL " + url + " has no host java.net.URI can parse");
      }
      // Case-insensitive, matching URI semantics and java.net.http's own check. Anything
      // stricter refuses HTTP:// and Https://, which the transport would have sent.
      String scheme = parsed.getScheme();
      if (scheme == null
          || !(scheme.equalsIgnoreCase("http") || scheme.equalsIgnoreCase("https"))) {
        throw new IllegalArgumentException("URL " + url + " is not http or https");
      }
      return parsed;
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
