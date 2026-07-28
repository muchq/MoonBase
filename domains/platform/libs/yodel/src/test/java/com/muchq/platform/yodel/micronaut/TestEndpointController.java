package com.muchq.platform.yodel.micronaut;

import io.micronaut.http.annotation.Controller;
import io.micronaut.http.annotation.Get;

@Controller("/yodel-test")
public class TestEndpointController {

  @Get("/ok")
  public String ok() {
    return "ok";
  }

  @Get("/boom")
  public String boom() {
    throw new RuntimeException("boom");
  }
}
