package com.muchq.indexer.api.dto;

public record QueryRequest(String query, int limit, int offset) {
    public QueryRequest {
        if (limit <= 0) limit = 50;
        if (limit > 1000) limit = 1000;
        if (offset < 0) offset = 0;
    }
}
