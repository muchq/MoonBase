package com.muchq.games.chessql.ast;

import java.util.List;

public record SequenceExpr(List<String> motifNames) implements Expr {}
