package com.muchq.chess_indexer.query;

import java.util.List;

public record CompiledQuery(String sql, List<Object> params) {}
