package com.muchq.games.one_d4.api;

import com.muchq.games.chessql.compiler.AggregateSpec;
import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.ParsedQuery;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.dto.AggregateRequest;
import com.muchq.games.one_d4.api.dto.AggregateResponse;
import com.muchq.games.one_d4.api.dto.AggregateRow;
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

/**
 * Aggregation over indexed games: a ChessQL filter plus group-by fields, returning counts per
 * group. This is what "most popular opening" questions compile to — without it, callers page out
 * every matching row and group client-side.
 */
@Singleton
@Path("/v1/aggregate")
public class AggregateController {
  private static final Logger LOG = LoggerFactory.getLogger(AggregateController.class);

  private final GameFeatureStore gameFeatureStore;
  private final SqlCompiler sqlCompiler;
  private final AggregateRequestValidator validator;

  public AggregateController(
      GameFeatureStore gameFeatureStore,
      SqlCompiler sqlCompiler,
      AggregateRequestValidator validator) {
    this.gameFeatureStore = gameFeatureStore;
    this.sqlCompiler = sqlCompiler;
    this.validator = validator;
  }

  @POST
  @Consumes(MediaType.APPLICATION_JSON)
  @Produces(MediaType.APPLICATION_JSON)
  public AggregateResponse aggregate(AggregateRequest request) {
    AggregateSpec spec = validator.validate(request);

    LOG.info(
        "POST /v1/aggregate query={} groupBy={} orderBy={} minGames={} limit={}",
        request.query(),
        request.groupBy(),
        spec.order().wireName(),
        spec.minGames(),
        request.limit());

    ParsedQuery parsed = Parser.parse(request.query());
    List<String> groupColumns = sqlCompiler.resolveGroupByColumns(request.groupBy());
    CompiledQuery compiled = sqlCompiler.compileAggregate(parsed, spec);

    // The spec decides both what the SELECT list carries and what the store reads back, so the
    // row shape cannot be agreed on twice and answered differently.
    List<AggregateRow> groups =
        gameFeatureStore.aggregate(
            compiled, groupColumns, spec.hasOutcomeMetrics(), request.limit());

    // Fewer groups came back than the limit allowed, so nothing was cut off and the totals are
    // already in hand: every matching group is present, and their counts sum to every matching
    // game. Only a result that filled the limit could be hiding a tail worth a second
    // COUNT-over-groups scan.
    if (groups.size() < request.limit()) {
      return new AggregateResponse(groups, groups.size(), sumCounts(groups), groups.size(), false);
    }

    CompiledQuery totalsQuery = sqlCompiler.compileAggregateTotals(parsed, spec);
    GameFeatureStore.AggregateTotals totals = gameFeatureStore.aggregateTotals(totalsQuery);
    return new AggregateResponse(
        groups,
        groups.size(),
        totals.totalGames(),
        totals.totalGroups(),
        totals.totalGroups() > groups.size());
  }

  private static long sumCounts(List<AggregateRow> groups) {
    return groups.stream().mapToLong(AggregateRow::count).sum();
  }
}
