package com.muchq.indexer.chessql.ast;

public record ComparisonExpr(String field, String operator, Object value) implements Expr {
}
