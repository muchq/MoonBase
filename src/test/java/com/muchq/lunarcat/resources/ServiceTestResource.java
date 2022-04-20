package com.muchq.lunarcat.resources;

import com.muchq.lunarcat.Widget;
import java.util.Optional;
import javax.ws.rs.BadRequestException;
import javax.ws.rs.GET;
import javax.ws.rs.NotFoundException;
import javax.ws.rs.Path;
import javax.ws.rs.Produces;
import javax.ws.rs.QueryParam;
import javax.ws.rs.core.MediaType;

@Path("test")
@Produces(MediaType.APPLICATION_JSON)
public class ServiceTestResource {

  @GET
  public Widget sayHi(@QueryParam("message") String message) {
    return new Widget(message);
  }

  @GET
  @Path("optional-present")
  public Optional<Widget> getPresentOptional() {
    return Optional.of(new Widget("hi"));
  }

  @GET
  @Path("optional-empty")
  public Optional<Widget> getEmptyOptional() {
    return Optional.empty();
  }

  @GET
  @Path("server-error")
  public void getServerError() {
    throw new RuntimeException();
  }

  @GET
  @Path("bad-request")
  public void getBadRequestError() {
    throw new BadRequestException();
  }

  @GET
  @Path("not-found")
  public void getNotFound() {
    throw new NotFoundException();
  }
}
