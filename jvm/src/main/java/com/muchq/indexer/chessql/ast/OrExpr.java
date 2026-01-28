package com.muchq.indexer.chessql.ast;

import java.util.List;

public record OrExpr(List<Expr> operands) implements Expr {
}
