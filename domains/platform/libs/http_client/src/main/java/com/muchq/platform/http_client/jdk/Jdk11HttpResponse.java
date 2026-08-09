package com.muchq.platform.http_client.jdk;

import com.muchq.platform.http_client.core.Header;
import com.muchq.platform.http_client.core.HttpRequest;
import com.muchq.platform.http_client.core.HttpResponse;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Objects;
import java.util.stream.Collectors;

public class Jdk11HttpResponse implements HttpResponse {

  private final HttpRequest request;
  private final java.net.http.HttpResponse<byte[]> delegate;

  public Jdk11HttpResponse(HttpRequest request, java.net.http.HttpResponse<byte[]> delegate) {
    this.request = Objects.requireNonNull(request);
    this.delegate = Objects.requireNonNull(delegate);
  }

  @Override
  public HttpRequest getRequest() {
    return request;
  }

  @Override
  public int getStatusCode() {
    return delegate.statusCode();
  }

  @Override
  public boolean isSuccess() {
    return delegate.statusCode() > 199 && delegate.statusCode() < 400;
  }

  @Override
  public boolean isError() {
    return !isSuccess();
  }

  @Override
  public boolean isClientError() {
    return delegate.statusCode() > 399 && delegate.statusCode() < 500;
  }

  @Override
  public boolean isServerError() {
    return delegate.statusCode() > 499;
  }

  @Override
  public List<Header> getHeaders() {
    return delegate.headers().map().entrySet().stream()
        .flatMap(entry -> entry.getValue().stream().map(value -> new Header(entry.getKey(), value)))
        .collect(Collectors.toList());
  }

  @Override
  public String getAsString() {
    return new String(getAsBytes(), StandardCharsets.UTF_8);
  }

  @Override
  public byte[] getAsBytes() {
    return delegate.body();
  }

  /**
   * The body, already in hand.
   *
   * <p>The whole response is read before {@code execute} returns, so this cannot block and cannot
   * outlive its deadline — which is the point (#1336). Callers that parse from a stream keep
   * working unchanged; they are just reading from memory now.
   */
  @Override
  public InputStream getAsInputStream() {
    return new java.io.ByteArrayInputStream(delegate.body());
  }
}
