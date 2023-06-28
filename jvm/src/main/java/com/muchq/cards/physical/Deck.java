package com.muchq.cards.physical;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Deque;
import java.util.List;
import java.util.Optional;

public record Deck(Deque<Card> cards) {

    public Optional<Card> nextCard() {
        if (cards.isEmpty()) {
            return Optional.empty();
        }
        return Optional.of(cards.removeLast());
    }

    public Deck shuffled() {
        List<Card> newCards = new ArrayList<>(cards);
        Collections.shuffle(newCards);
        return new Deck(new ArrayDeque<>(newCards));
    }

    public static Deck withJokers() {
        var cards = cardsListWithNoJokers();
        cards.add(new Card(Suit.JOKER, Rank.JOKER, Facing.DOWN));
        cards.add(new Card(Suit.JOKER, Rank.JOKER, Facing.DOWN));
        return new Deck(cards);
    }

    public static Deck noJokers() {
        return new Deck(cardsListWithNoJokers());
    }

    private static Deque<Card> cardsListWithNoJokers() {
        List<Card> cards = new ArrayList<>();
        for (var suit : Suit.values()) {
            for (var value : Rank.values()) {
                if (suit != Suit.JOKER && value != Rank.JOKER) {
                    cards.add(new Card(suit, value, Facing.DOWN));
                }
            }
        }
        return new ArrayDeque<>(cards);
    }
}
