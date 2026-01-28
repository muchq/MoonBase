package com.muchq.chess_indexer.query;

import java.util.List;

public record InExpr(Field field, List<Value> values) implements Expr {}
