package com.muchq.games.chessql.parser;

import com.muchq.games.chessql.ast.Expr;
import com.muchq.games.chessql.ast.OrderByClause;

/** The result of parsing a ChessQL query string: an expression plus an optional ORDER BY clause. */
public record ParsedQuery(Expr expr, OrderByClause orderBy) {}
