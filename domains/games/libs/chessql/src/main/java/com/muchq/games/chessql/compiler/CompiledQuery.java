package com.muchq.games.chessql.compiler;

import java.util.List;

public record CompiledQuery(String sql, List<Object> parameters) {}
