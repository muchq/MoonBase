package com.muchq.one_d4.chessql.ast;

import java.util.List;

public record AndExpr(List<Expr> operands) implements Expr {}
