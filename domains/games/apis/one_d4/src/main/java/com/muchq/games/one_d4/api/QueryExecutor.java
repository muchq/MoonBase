package com.muchq.games.one_d4.api;

import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.ParsedQuery;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.GameFeatureRow;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.api.dto.QueryRequest;
import com.muchq.games.one_d4.api.dto.QueryResponse;
import com.muchq.games.one_d4.db.GameFeatureStore;
import jakarta.inject.Singleton;
import java.util.List;
import java.util.Map;

/**
 * The query pipeline (parse, compile, run, attach occurrences), shared by {@link QueryController}
 * and {@link FirstPageWarmer} so the warmed response is built by exactly the code a live request
 * would run.
 */
@Singleton
public class QueryExecutor {
  private final GameFeatureStore gameFeatureStore;
  private final SqlCompiler queryCompiler;

  public QueryExecutor(GameFeatureStore gameFeatureStore, SqlCompiler queryCompiler) {
    this.gameFeatureStore = gameFeatureStore;
    this.queryCompiler = queryCompiler;
  }

  public QueryResponse execute(QueryRequest request) {
    ParsedQuery parsed = Parser.parse(request.query());
    CompiledQuery compiled = queryCompiler.compile(parsed, request.player());

    List<GameFeature> rows = gameFeatureStore.query(compiled, request.limit(), request.offset());

    List<String> gameUrls = rows.stream().map(GameFeature::gameUrl).toList();
    Map<String, Map<String, List<OccurrenceRow>>> occurrences =
        gameFeatureStore.queryOccurrences(gameUrls);

    List<GameFeatureRow> dtos =
        rows.stream()
            .map(
                row ->
                    GameFeatureRow.fromStore(
                        row, occurrences.getOrDefault(row.gameUrl(), Map.of())))
            .toList();

    return new QueryResponse(dtos, dtos.size());
  }
}
