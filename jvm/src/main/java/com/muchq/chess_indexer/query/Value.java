package com.muchq.chess_indexer.query;

public sealed interface Value permits NumberValue, StringValue, BooleanValue, IdentValue {
}
