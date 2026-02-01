package com.muchq.chess_indexer.query;

import com.muchq.chess_indexer.config.IndexerConfig;
import com.muchq.chess_indexer.db.GameDao;
import com.muchq.chess_indexer.model.GameSummary;
import jakarta.inject.Singleton;
import java.util.List;

@Singleton
public class QueryService {

  private final GameDao gameDao;
  private final IndexerConfig config;

  public QueryService(GameDao gameDao, IndexerConfig config) {
    this.gameDao = gameDao;
    this.config = config;
  }

  public List<GameSummary> run(String query) {
    Lexer lexer = new Lexer(query);
    Parser parser = new Parser(lexer.tokenize());
    Expr expr = parser.parse();

    QueryCompiler compiler = new QueryCompiler();
    CompiledQuery compiled = compiler.compile(expr, config.apiQueryLimit());

    return gameDao.query(compiled.sql(), compiled.params());
  }
}
