package com.muchq.chess_indexer.api;

import com.muchq.chess_indexer.model.GameSummary;
import com.muchq.chess_indexer.query.QueryService;
import io.micronaut.http.HttpResponse;
import io.micronaut.http.annotation.Body;
import io.micronaut.http.annotation.Controller;
import io.micronaut.http.annotation.Post;
import jakarta.inject.Inject;
import java.util.List;

@Controller("/queries")
public class QueryController {

  private final QueryService queryService;

  @Inject
  public QueryController(QueryService queryService) {
    this.queryService = queryService;
  }

  @Post
  public HttpResponse<QueryResponse> query(@Body QueryRequest request) {
    if (request == null || request.query() == null || request.query().isBlank()) {
      return HttpResponse.badRequest();
    }
    try {
      List<GameSummary> games = queryService.run(request.query());
      return HttpResponse.ok(QueryResponse.of(games));
    } catch (IllegalArgumentException e) {
      return HttpResponse.badRequest();
    }
  }
}
