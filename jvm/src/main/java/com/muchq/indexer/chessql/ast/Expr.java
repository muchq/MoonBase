package com.muchq.indexer.chessql.ast;

public sealed interface Expr permits OrExpr, AndExpr, NotExpr, ComparisonExpr, InExpr, MotifExpr {
}
