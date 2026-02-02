package com.muchq.one_d4.chessql.compiler;

import java.util.List;

public record CompiledQuery(String sql, List<Object> parameters) {
}
