package com.muchq.lunarcat.providers;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import java.io.OutputStream;
import java.lang.annotation.Annotation;
import java.lang.reflect.Type;
import javax.inject.Inject;
import javax.ws.rs.WebApplicationException;
import javax.ws.rs.core.MediaType;
import javax.ws.rs.core.MultivaluedMap;
import javax.ws.rs.ext.MessageBodyWriter;
import javax.ws.rs.ext.Provider;

@Provider
public class ErrorResponseMessageBodyWriter implements MessageBodyWriter<ErrorResponse> {

  private final ObjectMapper mapper;

  @Inject
  public ErrorResponseMessageBodyWriter(ObjectMapper mapper) {
    this.mapper = mapper;
  }

  @Override
  public boolean isWriteable(Class<?> type, Type genericType, Annotation[] annotations, MediaType mediaType) {
    return ErrorResponse.class.isAssignableFrom(type);
  }

  @Override
  public long getSize(
    ErrorResponse errorResponse,
    Class<?> type,
    Type genericType,
    Annotation[] annotations,
    MediaType mediaType
  ) {
    return 0;
  }

  @Override
  public void writeTo(
    ErrorResponse errorResponse,
    Class<?> type,
    Type genericType,
    Annotation[] annotations,
    MediaType mediaType,
    MultivaluedMap<String, Object> httpHeaders,
    OutputStream entityStream
  ) throws IOException, WebApplicationException {
    entityStream.write(mapper.writeValueAsBytes(errorResponse));
  }
}
