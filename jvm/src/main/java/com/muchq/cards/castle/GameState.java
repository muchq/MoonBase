package com.muchq.cards.castle;

import com.muchq.cards.model.Card;

import java.util.Deque;
import java.util.List;

public record GameState(Deque<Card> drawPile, List<Player> players, Turn lastPlayed) {
}
