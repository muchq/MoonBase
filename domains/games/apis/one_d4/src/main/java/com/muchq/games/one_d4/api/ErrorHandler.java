package com.muchq.games.one_d4.api;

import com.muchq.games.chessql.parser.ParseException;
import io.micronaut.http.HttpRequest;
import io.micronaut.http.HttpResponse;
import io.micronaut.http.annotation.Controller;
import io.micronaut.http.annotation.Error;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.NoSuchElementException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Controller
public class ErrorHandler {
  private static final Logger LOG = LoggerFactory.getLogger(ErrorHandler.class);

  @Error(global = true, exception = ParseException.class)
  public HttpResponse<Map<String, Object>> handleParseException(
      HttpRequest<?> request, ParseException ex) {
    Map<String, Object> body = new LinkedHashMap<>();
    body.put("error", ex.getMessage());
    body.put("position", ex.getPosition());
    return HttpResponse.badRequest(body);
  }

  @Error(global = true, exception = IllegalArgumentException.class)
  public HttpResponse<Map<String, Object>> handleIllegalArgument(
      HttpRequest<?> request, IllegalArgumentException ex) {
    return HttpResponse.badRequest(Map.of("error", ex.getMessage()));
  }

  @Error(global = true, exception = NoSuchElementException.class)
  public HttpResponse<Map<String, Object>> handleNotFound(
      HttpRequest<?> request, NoSuchElementException ex) {
    return HttpResponse.notFound(Map.of("error", ex.getMessage()));
  }

  @Error(global = true, exception = Exception.class)
  public HttpResponse<Map<String, Object>> handleGeneric(HttpRequest<?> request, Exception ex) {
    LOG.error("Unhandled exception on {} {}", request.getMethod(), request.getUri(), ex);
    return HttpResponse.serverError(Map.of("error", "Internal server error"));
  }
}
