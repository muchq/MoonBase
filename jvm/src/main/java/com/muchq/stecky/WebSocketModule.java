package com.muchq.stecky;

import com.google.inject.AbstractModule;

public class WebSocketModule extends AbstractModule {

  @Override
  protected void configure() {
    requestStaticInjection(GuiceConfigurator.class);
  }
}
