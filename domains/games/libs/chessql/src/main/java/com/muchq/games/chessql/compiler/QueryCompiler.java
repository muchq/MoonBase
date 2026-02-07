package com.muchq.games.chessql.compiler;

import com.muchq.games.chessql.ast.Expr;

public interface QueryCompiler<T> {
  T compile(Expr expr);
}
