package com.muchq.cards.golf;

import com.muchq.cards.Card;

/**
 * Score is sum of card values. Jacks are worth 0 and pairs cancel each other out. If player knocks and wins their
 * score is sum of card values - 1. Low score wins.
 */
public record PlayerState(Card topLeft, Card topRight, Card bottomLeft, Card bottomRight, int score, boolean knocked) {
}
