package com.muchq.games.one_d4.api;

import com.muchq.games.one_d4.api.dto.QueryRequest;
import com.muchq.games.one_d4.api.dto.QueryResponse;
import jakarta.inject.Singleton;
import jakarta.ws.rs.Consumes;
import jakarta.ws.rs.POST;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
@Path("/v1/query")
public class QueryController {
  private static final Logger LOG = LoggerFactory.getLogger(QueryController.class);

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
  public QueryResponse query(QueryRequest request) {
    validator.validate(request);

    LOG.info(
        "POST /v1/query query={} limit={} offset={} player={}",
        request.query(),
        request.limit(),
        request.offset(),
        request.player());

    // A default request is answered from (or, on a cold/expired miss, loads) the shared
    // snapshot; matches() guarantees the loader computes exactly this request. Everything else
    // runs live.
    if (firstPageCache.matches(request)) {
      return firstPageCache.get();
    }
    return queryExecutor.execute(request);
  }
}
