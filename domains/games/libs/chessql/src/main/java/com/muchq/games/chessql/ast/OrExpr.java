package com.muchq.games.chessql.ast;

import java.util.List;

public record OrExpr(List<Expr> operands) implements Expr {}
