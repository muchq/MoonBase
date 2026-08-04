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

  // A parameterized route: the one shape where the matched template and
  // the raw path differ, which is what the route label's bounded-
  // cardinality argument rests on (#1303).
  @Get("/widgets/{id}")
  public String widget(String id) {
    return id;
  }
}
