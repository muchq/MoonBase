package com.muchq.json;


import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.guice.ReinstallableGuiceModule;

public class ObjectMapperModule extends ReinstallableGuiceModule {
  @Override
  protected void configure() {
    bind(ObjectMapper.class).toInstance(JsonUtils.MAPPER);
  }
}
