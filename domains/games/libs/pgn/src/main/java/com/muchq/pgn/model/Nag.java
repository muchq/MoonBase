package com.muchq.pgn.model;

/**
 * Numeric Annotation Glyph - standard annotations like $1 (good move), $2 (poor move), etc.
 * Common NAGs:
 *   $1 = ! (good move)
 *   $2 = ? (poor move)
 *   $3 = !! (very good move)
 *   $4 = ?? (blunder)
 *   $5 = !? (interesting move)
 *   $6 = ?! (dubious move)
 */
public record Nag(int value) {

    public static Nag parse(String s) {
        throw new UnsupportedOperationException("TODO: implement");
    }

    @Override
    public String toString() {
        return "$" + value;
    }
}
