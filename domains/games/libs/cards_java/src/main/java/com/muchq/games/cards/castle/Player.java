package com.muchq.games.cards.castle;

import com.muchq.games.cards.Card;
import java.util.List;

public record Player(String name, List<Card> hand, ThreeUp faceUp, ThreeDown faceDown) {}
