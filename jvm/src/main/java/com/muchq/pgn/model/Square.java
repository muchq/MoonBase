package com.muchq.pgn.model;

public record Square(File file, Rank rank) {

    public static Square parse(String s) {
        throw new UnsupportedOperationException("TODO: implement");
    }

    @Override
    public String toString() {
        throw new UnsupportedOperationException("TODO: implement");
    }
}
