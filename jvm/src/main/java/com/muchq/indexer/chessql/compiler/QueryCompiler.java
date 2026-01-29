package com.muchq.indexer.chessql.compiler;

import com.muchq.indexer.chessql.ast.Expr;

public interface QueryCompiler<T> {
    T compile(Expr expr);
}
