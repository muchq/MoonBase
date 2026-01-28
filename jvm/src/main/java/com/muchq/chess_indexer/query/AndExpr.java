package com.muchq.chess_indexer.query;

public record AndExpr(Expr left, Expr right) implements Expr {}
