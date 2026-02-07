package com.muchq.games.cards.castle;

import com.muchq.games.cards.Card;
import java.util.Deque;
import java.util.List;

public record GameState(Deque<Card> drawPile, List<Player> players, Turn lastPlayed) {}
