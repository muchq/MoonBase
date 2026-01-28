package com.muchq.indexer.api;

import com.muchq.indexer.api.dto.GameFeatureRow;
import com.muchq.indexer.api.dto.QueryRequest;
import com.muchq.indexer.api.dto.QueryResponse;
import com.muchq.indexer.chessql.ast.Expr;
import com.muchq.indexer.chessql.compiler.CompiledQuery;
import com.muchq.indexer.chessql.compiler.SqlCompiler;
import com.muchq.indexer.chessql.parser.Parser;
import com.muchq.indexer.db.GameFeatureDao;
import jakarta.ws.rs.Consumes;
import jakarta.ws.rs.POST;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.List;

@Path("/query")
public class QueryController {
    private static final Logger LOG = LoggerFactory.getLogger(QueryController.class);

    private final GameFeatureDao gameFeatureDao;
    private final SqlCompiler sqlCompiler;

    public QueryController(GameFeatureDao gameFeatureDao, SqlCompiler sqlCompiler) {
        this.gameFeatureDao = gameFeatureDao;
        this.sqlCompiler = sqlCompiler;
    }

    @POST
    @Consumes(MediaType.APPLICATION_JSON)
    @Produces(MediaType.APPLICATION_JSON)
    public QueryResponse query(QueryRequest request) {
        LOG.info("POST /query query={} limit={} offset={}", request.query(), request.limit(), request.offset());

        Expr expr = Parser.parse(request.query());
        CompiledQuery compiled = sqlCompiler.compile(expr);

        List<com.muchq.indexer.db.GameFeatureDao.GameFeatureRow> rows =
                gameFeatureDao.query(compiled, request.limit(), request.offset());

        List<GameFeatureRow> dtos = rows.stream()
                .map(GameFeatureRow::fromDao)
                .toList();

        return new QueryResponse(dtos, dtos.size());
    }
}
