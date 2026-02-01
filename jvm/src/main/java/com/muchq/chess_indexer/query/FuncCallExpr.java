package com.muchq.chess_indexer.query;

import java.util.List;

public record FuncCallExpr(String name, List<Value> args) implements Expr {}
