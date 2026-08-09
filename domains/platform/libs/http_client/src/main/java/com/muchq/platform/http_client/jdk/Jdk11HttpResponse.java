package com.muchq.platform.http_client.jdk;

import com.muchq.platform.http_client.core.Header;
import com.muchq.platform.http_client.core.HttpRequest;
import com.muchq.platform.http_client.core.HttpResponse;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Objects;
import java.util.stream.Collectors;

public class Jdk11HttpResponse implements HttpResponse {

  private final HttpRequest request;
  private final java.net.http.HttpResponse<InputStream> delegate;

  public Jdk11HttpResponse(HttpRequest request, java.net.http.HttpResponse<InputStream> delegate) {
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
    try (InputStream inputStream = getAsInputStream()) {
      return inputStream.readAllBytes();
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  /**
   * The body, bounded when the request asked for it.
   *
   * <p>Wrapped here rather than buffered: the JDK's request deadline ended when the head arrived,
   * so a peer that answers and then stalls leaves the caller parked in this stream with no ceiling
   * at all (#1336). Streaming callers — the indexer reading a month of PGNs — keep streaming.
   */
  @Override
  public InputStream getAsInputStream() {
    return request
        .getBodyReadTimeout()
        .<InputStream>map(timeout -> new DeadlineInputStream(delegate.body(), timeout))
        .orElseGet(delegate::body);
  }
}
