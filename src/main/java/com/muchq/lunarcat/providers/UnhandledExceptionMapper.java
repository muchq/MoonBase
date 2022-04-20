package com.muchq.lunarcat.providers;

import javax.ws.rs.Produces;
import javax.ws.rs.WebApplicationException;
import javax.ws.rs.core.MediaType;
import javax.ws.rs.core.Response;
import javax.ws.rs.ext.ExceptionMapper;
import javax.ws.rs.ext.Provider;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Provider
@Produces({ MediaType.APPLICATION_JSON, MediaType.APPLICATION_OCTET_STREAM })
public class UnhandledExceptionMapper implements ExceptionMapper<Exception> {

  private static final Logger LOGGER = LoggerFactory.getLogger(UnhandledExceptionMapper.class);

  @Override
  public Response toResponse(Exception e) {
    if (e instanceof WebApplicationException) {
      return error(((WebApplicationException) e).getResponse().getStatus(), e.getMessage());
    }

    LOGGER.error("unhandled exception due to {}", e.getCause(), e);
    return error(500, "internal error");
  }

  private Response error(int status, String message) {
    return Response.status(status).type(MediaType.APPLICATION_JSON_TYPE).entity(new ErrorResponse(message)).build();
  }
}
