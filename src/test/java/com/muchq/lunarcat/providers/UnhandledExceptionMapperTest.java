package com.muchq.lunarcat.providers;

import org.junit.Before;
import org.junit.Test;

import javax.ws.rs.BadRequestException;
import javax.ws.rs.NotFoundException;
import javax.ws.rs.core.Response;
import javax.ws.rs.core.Response.Status;

import static org.assertj.core.api.Assertions.assertThat;

public class UnhandledExceptionMapperTest {
  private UnhandledExceptionMapper mapper;

  @Before
  public void setup() {
    mapper = new UnhandledExceptionMapper();
  }

  @Test
  public void itMapsNotFoundTo404() {
    Response response = mapper.toResponse(new NotFoundException());
    assertThat(response.getStatus()).isEqualTo(Status.NOT_FOUND.getStatusCode());
  }

  @Test
  public void itMapsBadRequestTo400() {
    Response response = mapper.toResponse(new BadRequestException());
    assertThat(response.getStatus()).isEqualTo(Status.BAD_REQUEST.getStatusCode());
  }

  @Test
  public void itMapsRuntimeExceptionTo500() {
    Response response = mapper.toResponse(new RuntimeException());
    assertThat(response.getStatus()).isEqualTo(Status.INTERNAL_SERVER_ERROR.getStatusCode());
  }

  @Test
  public void itMapsExceptionTo500() {
    Response response = mapper.toResponse(new Exception());
    assertThat(response.getStatus()).isEqualTo(Status.INTERNAL_SERVER_ERROR.getStatusCode());
  }
}
