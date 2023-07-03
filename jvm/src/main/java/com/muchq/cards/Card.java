package com.muchq.cards;

import java.util.List;

public record Card(Suit suit, Rank rank, Facing facing) {
    private static final List<Suit> SUITS = List.of(Suit.CLUBS, Suit.DIAMONDS, Suit.HEARTS, Suit.SPADES);
    private static final List<Rank> RANKS = List.of(
            Rank.TWO, Rank.THREE, Rank.FOUR, Rank.FIVE, Rank.SIX, Rank.SEVEN, Rank.EIGHT,
            Rank.NINE, Rank.TEN, Rank.JACK, Rank.QUEEN, Rank.KING, Rank.ACE);
    
    public static Card forIndex(int index) {
        Suit s = SUITS.get(index % 4);
        Rank r = RANKS.get(index % 13);
        return new Card(s, r, Facing.DOWN);
    }
}
