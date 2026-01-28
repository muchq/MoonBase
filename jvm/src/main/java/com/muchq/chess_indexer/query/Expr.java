package com.muchq.chess_indexer.query;

public sealed interface Expr permits AndExpr, OrExpr, NotExpr, CompareExpr, InExpr, FuncCallExpr {
}
