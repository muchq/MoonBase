package com.muchq.games.one_d4.api;

import com.muchq.games.chessql.parser.ParseException;
import io.micronaut.http.HttpRequest;
import io.micronaut.http.HttpResponse;
import io.micronaut.http.HttpStatus;
import io.micronaut.http.annotation.Controller;
import io.micronaut.http.annotation.Error;
import io.micronaut.http.server.exceptions.NotFoundException;
import io.micronaut.http.server.exceptions.UnsupportedMediaException;
import io.micronaut.json.JsonSyntaxException;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.NoSuchElementException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import tools.jackson.databind.DatabindException;

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

  @Error(global = true, exception = NotFoundException.class)
  public HttpResponse<Map<String, Object>> handleRouteMiss(
      HttpRequest<?> request, NotFoundException ex) {
    return HttpResponse.notFound(Map.of("error", "Not found"));
  }

  // A body that is not JSON is the caller's mistake, like a query that does not parse: 400
  // with the same envelope, not a 500 with a stack trace and a Sentry event per typo.
  @Error(global = true, exception = JsonSyntaxException.class)
  public HttpResponse<Map<String, Object>> handleBadJson(
      HttpRequest<?> request, JsonSyntaxException ex) {
    return HttpResponse.badRequest(Map.of("error", "Request body is not valid JSON"));
  }

  // Valid JSON that does not fit the request — a string where a number goes — reaches the
  // handler as Jackson's own exception rather than Micronaut's; the caller's mistake all the same.
  @Error(global = true, exception = DatabindException.class)
  public HttpResponse<Map<String, Object>> handleUnbindableBody(
      HttpRequest<?> request, DatabindException ex) {
    return HttpResponse.badRequest(
        Map.of("error", "Request body does not match the request shape"));
  }

  @Error(global = true, exception = UnsupportedMediaException.class)
  public HttpResponse<Map<String, Object>> handleUnsupportedMedia(
      HttpRequest<?> request, UnsupportedMediaException ex) {
    return HttpResponse.status(HttpStatus.UNSUPPORTED_MEDIA_TYPE);
  }

  @Error(global = true, exception = Exception.class)
  public HttpResponse<Map<String, Object>> handleGeneric(HttpRequest<?> request, Exception ex) {
    LOG.error("Unhandled exception on {} {}", request.getMethod(), request.getUri(), ex);
    return HttpResponse.serverError(Map.of("error", "Internal server error"));
  }
}
