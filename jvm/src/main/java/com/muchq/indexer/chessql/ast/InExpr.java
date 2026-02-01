package com.muchq.indexer.chessql.ast;

import java.util.List;

public record InExpr(String field, List<Object> values) implements Expr {
}
