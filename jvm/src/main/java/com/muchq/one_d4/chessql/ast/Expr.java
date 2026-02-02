package com.muchq.one_d4.chessql.ast;

public sealed interface Expr permits OrExpr, AndExpr, NotExpr, ComparisonExpr, InExpr, MotifExpr {
}
