package com.muchq.indexer.chessql.ast;

import java.util.List;

public record AndExpr(List<Expr> operands) implements Expr {
}
