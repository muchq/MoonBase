package com.muchq.cards.golf_ws;

public class Response {
  public Response() {}

  public static Response success(String message) {
    return new Response();
  }

  public static Response error(String message) {
    return new Response();
  }
}
