package com.muchq.helloworld;

import io.micronaut.runtime.Micronaut;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class App {
  private static final Logger LOG = LoggerFactory.getLogger(App.class);

  public static void main(String[] args) {
    LOG.info("Starting Micronaut hello-world HTTP server");
    Micronaut.run(App.class, args);
  }
}
