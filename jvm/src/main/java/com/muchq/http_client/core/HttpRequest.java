package com.muchq.http_client.core;

import static java.nio.charset.StandardCharsets.UTF_8;

import java.net.URI;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
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
  private final byte[] body;

  private HttpRequest(Method method, URI url, List<Header> headers, byte @Nullable [] body) {
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

  public byte[] getBody() {
    return body;
  }

  public static class Builder {
    private String url = null;
    private Method method = Method.GET;
    private final List<Header> headers = new ArrayList<>();
    private byte[] body = null;
    private ContentType contentType = ContentType.JSON;
    private ContentType accept = ContentType.JSON;

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

    public HttpRequest build() {
      URI url = buildUrl();
      List<Header> headers = buildHeaders();
      validateBodyState();

      return new HttpRequest(method, url, headers, body);
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
