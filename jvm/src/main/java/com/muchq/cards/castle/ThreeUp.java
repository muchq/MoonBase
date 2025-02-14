package com.muchq.cards.castle;

import com.muchq.cards.model.Card;

import java.util.Optional;

public record ThreeUp(Optional<Card> left, Optional<Card> center, Optional<Card> right) {
    public boolean isEmpty() {
        return left().isEmpty() && center().isEmpty() && right().isEmpty();
    }
}
