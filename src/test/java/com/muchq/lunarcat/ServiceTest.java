package com.muchq.lunarcat;

import com.hubspot.horizon.HttpClient;
import com.hubspot.horizon.HttpRequest;
import com.hubspot.horizon.HttpResponse;
import com.hubspot.horizon.apache.ApacheHttpClient;
import com.muchq.lunarcat.config.Configuration;
import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;

import java.net.ServerSocket;
import java.util.UUID;

import static org.assertj.core.api.Assertions.assertThat;

public class ServiceTest {
  private static Service service;
  private final static HttpClient client = new ApacheHttpClient();

  private static Integer port;

  @BeforeClass
  public static void setup() {
    Configuration configuration = Configuration.newBuilder()
        .withPort(getPort())
        .withBasePackage(Package.getPackage("com.muchq.lunarcat"))
        .build();
    service = new Service(configuration);
    service.runNoWait();
  }

  @AfterClass
  public static void tearDown() {
    service.shutDown();
  }

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
        .setUrl("http://localhost:" + getPort() + "/test/optional-empty").build();
    HttpResponse response = getClient().execute(request);
    assertThat(response.getStatusCode()).isEqualTo(404);
  }

  @Test
  public void itReturns404OnUnboundPath() {
    String path = UUID.randomUUID().toString();
    HttpRequest request = HttpRequest.newBuilder()
        .setUrl("http://localhost:" + getPort() + "/" + path).build();
    HttpResponse response = getClient().execute(request);
    assertThat(response.getStatusCode()).isEqualTo(404);
  }

  @Test
  public void itReturns404OnNotFound() {
    HttpRequest request = HttpRequest.newBuilder()
        .setUrl("http://localhost:" + getPort() + "/test/not-found").build();
    HttpResponse response = getClient().execute(request);
    assertThat(response.getStatusCode()).isEqualTo(404);
  }

  @Test
  public void itReturns500OnServerError() {
    HttpRequest request = HttpRequest.newBuilder()
        .setUrl("http://localhost:" + getPort() + "/test/server-error").build();
    HttpResponse response = getClient().execute(request);
    assertThat(response.getStatusCode()).isEqualTo(500);
  }

  @Test
  public void itReturns400OnBadRequest() {
    HttpRequest request = HttpRequest.newBuilder()
        .setUrl("http://localhost:" + getPort() + "/test/bad-request").build();
    HttpResponse response = getClient().execute(request);
    assertThat(response.getStatusCode()).isEqualTo(400);
  }

  public static HttpClient getClient() {
    return client;
  }

  public static int getPort() {
    if (port == null) {
      port = getAvailablePort();
    }
    return port;
  }

  private static int getAvailablePort() {
    try {
      ServerSocket serverSocket = new ServerSocket(0);
      int port = serverSocket.getLocalPort();
      serverSocket.close();
      return port;
    } catch (Exception e) {
      throw new RuntimeException();
    }
  }
}
