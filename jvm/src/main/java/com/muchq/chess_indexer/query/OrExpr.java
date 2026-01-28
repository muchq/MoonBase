package com.muchq.chess_indexer.query;

public record OrExpr(Expr left, Expr right) implements Expr {}
