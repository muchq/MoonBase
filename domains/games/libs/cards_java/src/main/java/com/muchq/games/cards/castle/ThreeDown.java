package com.muchq.games.cards.castle;

import com.muchq.games.cards.Card;
import java.util.Optional;

public record ThreeDown(Optional<Card> left, Optional<Card> center, Optional<Card> right) {
  public boolean isEmpty() {
    return left().isEmpty() && center().isEmpty() && right().isEmpty();
  }
}
