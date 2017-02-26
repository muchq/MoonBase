package com.muchq.lunarcat;

import com.hubspot.horizon.HttpClient;
import com.hubspot.horizon.apache.ApacheHttpClient;
import com.muchq.lunarcat.config.Configuration;
import org.junit.AfterClass;
import org.junit.BeforeClass;

import java.net.ServerSocket;

public class ServiceTestBase {
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
