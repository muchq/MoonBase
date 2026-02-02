package com.muchq.one_d4.chessql.compiler;

import com.muchq.one_d4.chessql.ast.Expr;

public interface QueryCompiler<T> {
    T compile(Expr expr);
}
