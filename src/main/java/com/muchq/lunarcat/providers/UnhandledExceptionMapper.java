package com.muchq.lunarcat.providers;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.ws.rs.Produces;
import javax.ws.rs.WebApplicationException;
import javax.ws.rs.core.MediaType;
import javax.ws.rs.core.Response;
import javax.ws.rs.ext.ExceptionMapper;
import javax.ws.rs.ext.Provider;

@Provider
@Produces({MediaType.APPLICATION_JSON, MediaType.APPLICATION_OCTET_STREAM})
public class UnhandledExceptionMapper implements ExceptionMapper<Exception> {
  private static final Logger LOGGER = LoggerFactory.getLogger(UnhandledExceptionMapper.class);

  @Override
  public Response toResponse(Exception e) {
    if (e instanceof WebApplicationException) {
      return ((WebApplicationException) e).getResponse();
    }

    LOGGER.error("unhandled exception due to {}", e.getCause(), e);
    return error(500).entity(new ErrorResponse("internal error")).build();
  }

  private static class ErrorResponse {
    private final String message;

    private ErrorResponse(String message) {
      this.message = message;
    }

    public String getMessage() {
      return message;
    }
  }

  private Response.ResponseBuilder error(int status) {
    return Response.status(status).type(MediaType.APPLICATION_JSON_TYPE);
  }
}

