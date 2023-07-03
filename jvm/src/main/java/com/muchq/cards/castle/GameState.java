package com.muchq.cards.castle;

import com.muchq.cards.Card;

import java.util.Deque;

public record GameState(Deque<Card> deck) {
}
