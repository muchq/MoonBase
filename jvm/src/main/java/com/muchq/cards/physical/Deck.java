package com.muchq.cards.physical;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.Deque;
import java.util.List;
import java.util.Optional;

public class Deck {
    private final Deque<Card> cards;

    public Deck(Collection<Card> cards) {
        this.cards = new ArrayDeque<>(cards);
    }

    public Optional<Card> nextCard() {
        if (cards.isEmpty()) {
            return Optional.empty();
        }
        return Optional.of(cards.removeLast());
    }

    public Deck shuffled() {
        List<Card> newCards = new ArrayList<>(cards);
        Collections.shuffle(newCards);
        return new Deck(newCards);
    }

    public static Deck withJokers() {
        var cards = cardsListWithNoJokers();
        cards.add(new Card(Suit.JOKER, Value.JOKER, Facing.DOWN));
        cards.add(new Card(Suit.JOKER, Value.JOKER, Facing.DOWN));
        return new Deck(cards);
    }

    public static Deck noJokers() {
        return new Deck(cardsListWithNoJokers());
    }

    private static List<Card> cardsListWithNoJokers() {
        List<Card> cards = new ArrayList<>();
        for (var suit : Suit.values()) {
            for (var value : Value.values()) {
                if (suit != Suit.JOKER && value != Value.JOKER) {
                    cards.add(new Card(suit, value, Facing.DOWN));
                }
            }
        }
        return cards;
    }
}
