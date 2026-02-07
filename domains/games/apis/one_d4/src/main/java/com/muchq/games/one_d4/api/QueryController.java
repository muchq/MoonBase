package com.muchq.games.one_d4.api;

import com.muchq.games.chessql.ast.Expr;
import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.chessql.compiler.QueryCompiler;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.GameFeatureRow;
import com.muchq.games.one_d4.api.dto.QueryRequest;
import com.muchq.games.one_d4.api.dto.QueryResponse;
import com.muchq.games.one_d4.db.GameFeatureStore;
import jakarta.inject.Singleton;
import jakarta.ws.rs.Consumes;
import jakarta.ws.rs.POST;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;
import java.util.List;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
@Path("/query")
public class QueryController {
  private static final Logger LOG = LoggerFactory.getLogger(QueryController.class);

  private final GameFeatureStore gameFeatureStore;
  private final QueryCompiler<CompiledQuery> queryCompiler;

  public QueryController(
      GameFeatureStore gameFeatureStore, QueryCompiler<CompiledQuery> queryCompiler) {
    this.gameFeatureStore = gameFeatureStore;
    this.queryCompiler = queryCompiler;
  }

  @POST
  @Consumes(MediaType.APPLICATION_JSON)
  @Produces(MediaType.APPLICATION_JSON)
  public QueryResponse query(QueryRequest request) {
    LOG.info(
        "POST /query query={} limit={} offset={}",
        request.query(),
        request.limit(),
        request.offset());

    Expr expr = Parser.parse(request.query());
    CompiledQuery compiled = queryCompiler.compile(expr);

    List<GameFeature> rows = gameFeatureStore.query(compiled, request.limit(), request.offset());

    List<GameFeatureRow> dtos = rows.stream().map(GameFeatureRow::fromStore).toList();

    return new QueryResponse(dtos, dtos.size());
  }
}
