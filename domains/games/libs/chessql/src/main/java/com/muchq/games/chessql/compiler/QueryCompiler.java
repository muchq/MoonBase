package com.muchq.games.chessql.compiler;

import com.muchq.games.chessql.parser.ParsedQuery;

public interface QueryCompiler<T> {
  T compile(ParsedQuery query);
}
