package com.muchq.chess_indexer.api;

import com.muchq.chess_indexer.db.IndexRequestDao;
import com.muchq.chess_indexer.ingest.IndexRequestService;
import com.muchq.chess_indexer.model.IndexRequest;
import io.micronaut.http.HttpResponse;
import io.micronaut.http.annotation.Body;
import io.micronaut.http.annotation.Controller;
import io.micronaut.http.annotation.Get;
import io.micronaut.http.annotation.PathVariable;
import io.micronaut.http.annotation.Post;
import jakarta.inject.Inject;
import java.time.LocalDate;
import java.util.Optional;
import java.util.UUID;

@Controller("/index-requests")
public class IndexRequestController {

  private final IndexRequestService indexRequestService;
  private final IndexRequestDao indexRequestDao;

  @Inject
  public IndexRequestController(IndexRequestService indexRequestService, IndexRequestDao indexRequestDao) {
    this.indexRequestService = indexRequestService;
    this.indexRequestDao = indexRequestDao;
  }

  @Post
  public HttpResponse<IndexRequestResponse> create(@Body IndexRequestCreateRequest request) {
    try {
      String platform = normalizePlatform(request.platform());
      LocalDate startDate = parseDate(request.startDate());
      LocalDate endDate = parseDate(request.endDate());
      if (endDate.isBefore(startDate)) {
        return HttpResponse.badRequest();
      }

      IndexRequest created = indexRequestService.submit(platform, request.username(), startDate, endDate);
      return HttpResponse.created(IndexRequestResponse.from(created));
    } catch (IllegalArgumentException e) {
      return HttpResponse.badRequest();
    }
  }

  @Get("/{id}")
  public HttpResponse<IndexRequestResponse> get(@PathVariable String id) {
    Optional<IndexRequest> request = indexRequestDao.findById(UUID.fromString(id));
    return request.map(value -> HttpResponse.ok(IndexRequestResponse.from(value)))
        .orElseGet(HttpResponse::notFound);
  }

  private String normalizePlatform(String platform) {
    if (platform == null) {
      throw new IllegalArgumentException("platform is required");
    }
    String normalized = platform.trim().toLowerCase();
    if (!normalized.equals("chess.com")) {
      throw new IllegalArgumentException("Only chess.com is supported right now");
    }
    return normalized;
  }

  private LocalDate parseDate(String value) {
    if (value == null || value.isBlank()) {
      throw new IllegalArgumentException("date is required");
    }
    return LocalDate.parse(value);
  }
}
