package com.muchq.games.chessql.compiler;

import java.util.List;

/** A compiled ChessQL query: a full SELECT SQL statement and its bound parameters. */
public record CompiledQuery(String selectSql, List<Object> parameters) {}
