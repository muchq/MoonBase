package com.muchq.cards.model;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Deque;
import java.util.List;

public record Deck(Deque<Card> cards) {

    public boolean hasNext() {
        return !cards.isEmpty();
    }

    public Card nextCard() {
        return cards.removeLast();
    }

    public Deck shuffled() {
        List<Card> newCards = new ArrayList<>(cards);
        Collections.shuffle(newCards);
        return new Deck(new ArrayDeque<>(newCards));
    }

    public static Deck withJokers() {
        var cards = cardsListWithNoJokers();
        cards.add(new Card(Suit.NONE, Rank.JOKER));
        cards.add(new Card(Suit.NONE, Rank.JOKER));
        return new Deck(cards);
    }

    public static Deck noJokers() {
        return new Deck(cardsListWithNoJokers());
    }

    private static Deque<Card> cardsListWithNoJokers() {
        List<Card> cards = new ArrayList<>();
        for (var suit : Suit.values()) {
            for (var value : Rank.values()) {
                if (suit != Suit.NONE && value != Rank.JOKER) {
                    cards.add(new Card(suit, value));
                }
            }
        }
        return new ArrayDeque<>(cards);
    }
}
