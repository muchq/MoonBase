package com.muchq.json;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Guice;
import org.junit.Test;

import static org.assertj.core.api.Assertions.assertThat;

public class ObjectMapperModuleTest {
  @Test
  public void itProvidesConfiguredMapperForUnannotatedInjections() {
    ObjectMapper mapper = Guice.createInjector(new ObjectMapperModule()).getInstance(ObjectMapper.class);
    assertThat(mapper).isSameAs(JsonUtils.MAPPER);
  }
}
