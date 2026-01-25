package com.muchq.pgn.model;

import java.util.List;
import java.util.Optional;

/**
 * A complete parsed PGN game.
 */
public record PgnGame(
    List<TagPair> tags,
    List<Move> moves,
    GameResult result
) {
    /**
     * Get a tag value by name.
     */
    public Optional<String> getTag(String name) {
        return tags.stream()
            .filter(t -> t.name().equals(name))
            .map(TagPair::value)
            .findFirst();
    }
}
