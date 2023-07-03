package com.muchq.cards.golf;

import com.muchq.cards.Card;

import java.util.Deque;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public record GameState(Deque<Card> drawPile, Deque<Card> discardPile, List<PlayerState> playerStates, int whoseTurn, int whoKnocked) {
    public boolean isOver() {
        return cards.isEmpty() || whoKnocked == whoseTurn;
    }

    /**
     * Ties are possible if no one knocks and there's a tie score.
     * If someone knocks and there's a tie, tie goes to the runner.
     * @return indexes of winners
     */
    public Set<Integer> whoWon() {
        Set<Integer> potentialWinnerIndexes = new HashSet<>();
        int minScore = Integer.MAX_VALUE;
        for (int i=0; i<playerStates.size(); i++) {
            var player = playerStates.get(i);
            if (player.score() < minScore) {
                potentialWinnerIndexes.clear();
                potentialWinnerIndexes.add(i);
                minScore = player.score();
            } else if (player.score() == minScore) {
                potentialWinnerIndexes.add(i);
            }
        }
        
        if (potentialWinnerIndexes.contains(whoKnocked)) {
            return Set.of(whoKnocked);
        }
        return potentialWinnerIndexes;
    }
}
