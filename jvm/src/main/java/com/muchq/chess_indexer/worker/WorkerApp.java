package com.muchq.chess_indexer.worker;

import io.micronaut.context.ApplicationContext;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class WorkerApp {
  private static final Logger LOG = LoggerFactory.getLogger(WorkerApp.class);

  public static void main(String[] args) {
    LOG.info("Starting chess indexer worker");
    try (ApplicationContext context = ApplicationContext.run()) {
      context.getBean(IndexerWorker.class).runForever();
    }
  }
}
