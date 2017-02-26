package com.muchq.lunarcat;

import com.hubspot.horizon.HttpRequest;
import com.hubspot.horizon.HttpResponse;
import org.junit.Test;

import java.util.UUID;

import static org.assertj.core.api.Assertions.assertThat;

public class ServiceTest extends ServiceTestBase {

  @Test
  public void itServesRequests() {
    String message = "hey";
    HttpRequest request = HttpRequest.newBuilder().setUrl("http://localhost:" + getPort() + "/test?message=" + message).build();
    Widget widget = getClient().execute(request).getAs(Widget.class);
    assertThat(widget.getMessage()).isEqualTo(message);
  }

  @Test
  public void itWritesOptionalResponses() {
    HttpRequest request = HttpRequest.newBuilder().setUrl("http://localhost:" + getPort() + "/test/optional-present").build();
    HttpResponse response = getClient().execute(request);
    assertThat(response.getStatusCode()).isEqualTo(200);
  }

  @Test
  public void itReturns404OnEmptyOptionals() {
    HttpRequest request = HttpRequest.newBuilder()
        .setUrl("http://localhost:" + getPort() + "/test/optional-empty")
        .setMaxRetries(0)
        .build();
    HttpResponse response = getClient().execute(request);
    assertThat(response.getStatusCode()).isEqualTo(404);
  }

  @Test
  public void itReturns404OnUnboundPath() {
    String path = UUID.randomUUID().toString();
    HttpRequest request = HttpRequest.newBuilder()
        .setUrl("http://localhost:" + getPort() + "/" + path)
        .setMaxRetries(0)
        .build();
    HttpResponse response = getClient().execute(request);
    assertThat(response.getStatusCode()).isEqualTo(404);
  }

  @Test
  public void itReturns404OnNotFound() {
    HttpRequest request = HttpRequest.newBuilder()
        .setUrl("http://localhost:" + getPort() + "/test/not-found")
        .setMaxRetries(0)
        .build();
    HttpResponse response = getClient().execute(request);
    assertThat(response.getStatusCode()).isEqualTo(404);
  }

  @Test
  public void itReturns500OnServerError() {
    HttpRequest request = HttpRequest.newBuilder()
        .setUrl("http://localhost:" + getPort() + "/test/server-error")
        .setMaxRetries(0)
        .build();
    HttpResponse response = getClient().execute(request);
    assertThat(response.getStatusCode()).isEqualTo(500);
  }

  @Test
  public void itReturns400OnBadRequest() {
    HttpRequest request = HttpRequest.newBuilder()
        .setUrl("http://localhost:" + getPort() + "/test/bad-request")
        .setMaxRetries(0)
        .build();
    HttpResponse response = getClient().execute(request);
    assertThat(response.getStatusCode()).isEqualTo(400);
  }
}
