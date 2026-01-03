package com.muchq.helloworld;

import jakarta.ws.rs.GET;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Path("/")
public class HelloWorldResource {
  private static final Logger logger = LoggerFactory.getLogger(HelloWorldResource.class);

  @GET
  @Produces(MediaType.APPLICATION_JSON)
  public HelloResponse hello() {
    logger.info("GET /");
    return new HelloResponse("Hello, World!", "/", "GET");
  }

  public record HelloResponse(String message, String path, String method) {}
}
