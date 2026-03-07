package com.muchq.games.chessql.ast;

public sealed interface Expr permits OrExpr, AndExpr, NotExpr, ComparisonExpr, InExpr, MotifExpr {}
