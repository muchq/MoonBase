package com.muchq.lunarcat.providers;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import javax.ws.rs.ext.ContextResolver;
import javax.ws.rs.ext.Provider;

@Provider
public class ObjectMapperContextResolver implements ContextResolver<ObjectMapper> {

  private final ObjectMapper mapper;

  @Inject
  public ObjectMapperContextResolver(ObjectMapper mapper) {
    this.mapper = mapper;
  }

  @Override
  public ObjectMapper getContext(Class<?> aClass) {
    return mapper;
  }
}
