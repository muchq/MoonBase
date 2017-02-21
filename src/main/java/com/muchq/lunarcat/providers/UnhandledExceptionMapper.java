package com.muchq.lunarcat.providers;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.ws.rs.BadRequestException;
import javax.ws.rs.NotFoundException;
import javax.ws.rs.Produces;
import javax.ws.rs.core.MediaType;
import javax.ws.rs.core.Response;
import javax.ws.rs.ext.ExceptionMapper;
import javax.ws.rs.ext.Provider;

@Provider
@Produces(MediaType.APPLICATION_JSON)
public class UnhandledExceptionMapper implements ExceptionMapper<Exception> {
  private static final Logger LOG = LoggerFactory.getLogger(UnhandledExceptionMapper.class);

  @Override
  public Response toResponse(Exception e) {
    if (e instanceof NotFoundException) {
      return Response.status(404).entity(new ErrorResponse("not found")).build();
    }

    if (e instanceof BadRequestException) {
      return Response.status(400).entity(new ErrorResponse("bad request")).build();
    }

    LOG.error("unhandled exception", e);
    return Response.serverError().entity(new ErrorResponse("internal error")).build();
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
}

