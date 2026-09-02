package com.muchq.games.one_d4.api;

import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.dto.QueryRequest;
import com.muchq.games.one_d4.api.dto.QueryResponse;
import io.micronaut.core.annotation.Nullable;
import jakarta.inject.Singleton;
import jakarta.ws.rs.Consumes;
import jakarta.ws.rs.HeaderParam;
import jakarta.ws.rs.POST;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;

@Singleton
@Path("/v1/query")
public class QueryController {
  private final QueryExecutor queryExecutor;
  private final QueryRequestValidator validator;
  private final FirstPageCache firstPageCache;

  public QueryController(
      QueryExecutor queryExecutor, QueryRequestValidator validator, FirstPageCache firstPageCache) {
    this.queryExecutor = queryExecutor;
    this.validator = validator;
    this.firstPageCache = firstPageCache;
  }

  @POST
  @Consumes(MediaType.APPLICATION_JSON)
  @Produces(MediaType.APPLICATION_JSON)
  public QueryResponse query(
      QueryRequest request,
      @HeaderParam("User-Agent") @Nullable String userAgent,
      @HeaderParam("Origin") @Nullable String origin) {
    // The query event replaces the old access-style line that logged the raw query text: the
    // shape is what the stats pipeline needs, and the text is the caller's (#1465).
    QueryEvent event = QueryEvent.start(QueryEvent.ENTRY_QUERY, userAgent, origin);
    try {
      validator.validate(request);
      event
          .shape(Parser.parse(request.query()))
          .put("player", request.player() != null)
          .put("limit", request.limit())
          .put("offset", request.offset());

      // A default request is answered from (or, on a cold/expired miss, loads) the shared
      // snapshot; matches() guarantees the loader computes exactly this request. Everything else
      // runs live.
      QueryResponse response;
      if (firstPageCache.matches(request)) {
        event.put("cache", QueryEvent.CACHE_SNAPSHOT);
        response = firstPageCache.get();
      } else {
        event.put("cache", QueryEvent.CACHE_LIVE);
        response = queryExecutor.execute(request);
      }
      event.put("rows", response.count()).finish(QueryEvent.OUTCOME_OK);
      return response;
    } catch (RuntimeException e) {
      event.finish(QueryEvent.outcomeOf(e));
      throw e;
    }
  }
}
