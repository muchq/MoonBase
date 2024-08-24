package com.muchq.cards.castle;

import com.muchq.cards.model.Card;

import java.util.List;

public record Player(String name, List<Card> hand, ThreeUp faceUp, ThreeDown faceDown) {
}
