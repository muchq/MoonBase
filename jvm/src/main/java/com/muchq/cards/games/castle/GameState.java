package com.muchq.cards.games.castle;

import com.muchq.cards.physical.Card;

import java.util.Deque;

public record GameState(Deque<Card> deck) {
}
