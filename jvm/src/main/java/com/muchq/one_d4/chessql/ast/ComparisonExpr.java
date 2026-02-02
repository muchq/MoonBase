package com.muchq.one_d4.chessql.ast;

public record ComparisonExpr(String field, String operator, Object value) implements Expr {
}
