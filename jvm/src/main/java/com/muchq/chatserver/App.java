package com.muchq.chatserver;

import io.micronaut.runtime.Micronaut;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class App {
  private static final Logger LOG = LoggerFactory.getLogger(App.class);

  public static void main(String[] args) {
    LOG.info("Starting Micronaut WebSocket chat server");
    Micronaut.run(App.class, args);
  }
}
