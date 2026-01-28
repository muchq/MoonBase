package com.muchq.chess_indexer.query;

public record CompareExpr(Field field, CompareOp op, Value value) implements Expr {}
