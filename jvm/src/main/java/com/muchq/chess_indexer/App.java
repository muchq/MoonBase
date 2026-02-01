package com.muchq.chess_indexer;

import io.micronaut.runtime.Micronaut;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class App {
  private static final Logger LOG = LoggerFactory.getLogger(App.class);

  public static void main(String[] args) {
    LOG.info("Starting chess indexer API");
    Micronaut.run(App.class, args);
  }
}
