package com.muchq.games.chessql.ast;

import java.util.List;

public record AndExpr(List<Expr> operands) implements Expr {}
