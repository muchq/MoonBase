package com.muchq.cards.castle;

import com.muchq.cards.Card;

import java.util.List;

public record Player(String name, List<Card> hand, ThreeUp faceUp, ThreeDown faceDown) {
}
