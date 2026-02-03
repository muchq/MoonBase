package com.muchq.stecky;

import com.google.inject.Injector;
import javax.inject.Inject;
import javax.websocket.server.ServerEndpointConfig;

public class GuiceConfigurator extends ServerEndpointConfig.Configurator {

  @Inject private static Injector injector;

  @Override
  public <T> T getEndpointInstance(Class<T> endpointClass) {
    return injector.getInstance(endpointClass);
  }
}
