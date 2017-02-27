package com.muchq.lunarcat.lifecycle.tasks;

import com.muchq.lunarcat.lifecycle.StartupTask;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class SayHiTask implements StartupTask {
  private static final Logger LOGGER = LoggerFactory.getLogger(SayHiTask.class);
  @Override
  public void execute() {
    LOGGER.info("hi");
  }
}
