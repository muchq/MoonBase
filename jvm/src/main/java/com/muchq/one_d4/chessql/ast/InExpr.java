package com.muchq.one_d4.chessql.ast;

import java.util.List;

public record InExpr(String field, List<Object> values) implements Expr {
}
