package com.muchq.lunarcat;

import com.hubspot.horizon.HttpClient;
import com.hubspot.horizon.HttpRequest;
import com.hubspot.horizon.HttpRequest.Method;
import com.hubspot.horizon.HttpResponse;
import com.hubspot.horizon.apache.ApacheHttpClient;
import com.muchq.lunarcat.Service.ServerMode;
import com.muchq.lunarcat.config.Configuration;
import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;

import java.net.ServerSocket;

import static org.assertj.core.api.Assertions.assertThat;

public class ServiceTest {
  private static Service service;
  private final static HttpClient client = new ApacheHttpClient();
  private static String baseUrl;

  @BeforeClass
  public static void setup() {
    int port = getPort();
    baseUrl = "http://localhost:" + port;
    Configuration configuration = Configuration.newBuilder()
        .withPort(port)
        .withBasePackage(Package.getPackage("com.muchq.lunarcat"))
        .build();
    service = new Service(configuration);
    service.run(ServerMode.NO_WAIT);
  }

  @AfterClass
  public static void tearDown() {
    service.shutDown();
  }

  @Test
  public void itServesRequests() {
    String message = "hey";
    HttpRequest request = HttpRequest.newBuilder().setUrl(baseUrl + "/test?message=" + message).build();
    Widget widget = client.execute(request).getAs(Widget.class);
    assertThat(widget.getMessage()).isEqualTo(message);
  }

  @Test
  public void itWritesOptionalResponses() {
    HttpRequest request = HttpRequest.newBuilder().setUrl(baseUrl + "/test/optional-present").build();
    HttpResponse response = client.execute(request);
    assertThat(response.getStatusCode()).isEqualTo(200);
  }

  @Test
  public void itReturns404OnEmptyOptionals() {
    HttpRequest request = HttpRequest.newBuilder()
        .setUrl(baseUrl + "/test/optional-empty").build();
    HttpResponse response = client.execute(request);
    assertThat(response.getStatusCode()).isEqualTo(404);
  }

  @Test
  public void itReturns404OnUnboundPath() {
    HttpRequest request = HttpRequest.newBuilder()
        .setUrl(baseUrl + "/this-is-not-a-real-path").build();
    HttpResponse response = client.execute(request);
    assertThat(response.getStatusCode()).isEqualTo(404);
  }

  @Test
  public void itReturns404OnNotFound() {
    HttpRequest request = HttpRequest.newBuilder()
        .setUrl(baseUrl + "/test/not-found").build();
    HttpResponse response = client.execute(request);
    assertThat(response.getStatusCode()).isEqualTo(404);
  }

  @Test
  public void itReturns500OnServerError() {
    HttpRequest request = HttpRequest.newBuilder()
        .setUrl(baseUrl + "/test/server-error").build();
    HttpResponse response = client.execute(request);
    assertThat(response.getStatusCode()).isEqualTo(500);
  }

  @Test
  public void itReturns405OnMethodNotAllowed() {
    HttpRequest request = HttpRequest.newBuilder()
        .setUrl(baseUrl + "/test/server-error")
        .setMethod(Method.POST)
        .build();
    HttpResponse response = client.execute(request);
    assertThat(response.getStatusCode()).isEqualTo(405);
  }

  @Test
  public void itReturns400OnBadRequest() {
    HttpRequest request = HttpRequest.newBuilder()
        .setUrl(baseUrl + "/test/bad-request").build();
    HttpResponse response = client.execute(request);
    assertThat(response.getStatusCode()).isEqualTo(400);
  }

  private static int getPort() {
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
