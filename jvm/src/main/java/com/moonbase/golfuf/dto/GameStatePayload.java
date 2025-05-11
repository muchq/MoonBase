package com.moonbase.golfuf.dto;

import java.util.List;
import java.util.Map;

// This will be a simplified version of gRPC GameState for client consumption
public class GameStatePayload implements WebSocketPayload {
    private boolean allHere;
    private int discardSize;
    private int drawSize;
    private String gameId;
    private String version;
    private boolean gameStarted;
    private boolean gameOver;
    private String knocker; // Optional
    private VisibleHandPayload hand; // Optional, player specific
    private int numberOfPlayers;
    private List<String> players;
    private List<Integer> scores; // Corresponds to players list by index
    private CardPayload topDiscard; // Optional
    private CardPayload topDraw; // Optional, player specific, if it's their turn & they drew
    private boolean yourTurn;
    private String currentTurnPlayerId; // ID of the player whose turn it is

    // Nested DTOs for Card and Hand
    public static class CardPayload {
        private String suit;
        private String rank;
        // Constructor, getters, setters
        public CardPayload(String suit, String rank) {
            this.suit = suit;
            this.rank = rank;
        }
        public String getSuit() { return suit; }
        public void setSuit(String suit) { this.suit = suit; }
        public String getRank() { return rank; }
        public void setRank(String rank) { this.rank = rank; }
    }

    public static class VisibleHandPayload {
        private CardPayload topLeft;
        private CardPayload topRight;
        private CardPayload bottomLeft;
        private CardPayload bottomRight;
        // Constructor, getters, setters for 4 cards
        // For simplicity, gRPC VisibleHand only has bottom_left, bottom_right.
        // We might expand this if UI shows all 4 cards once revealed.
        // For now, let's match the proto, but it will likely need to be 4 cards.
        // Let's assume for now the client side `script.js` expects up to 4 cards in hand.
        // The gRPC `VisibleHand` might be what *this* player can see of *their own* hand initially.
        // Let's design the DTO for what the client will render.

        public CardPayload getTopLeft() { return topLeft; }
        public void setTopLeft(CardPayload topLeft) { this.topLeft = topLeft; }
        public CardPayload getTopRight() { return topRight; }
        public void setTopRight(CardPayload topRight) { this.topRight = topRight; }
        public CardPayload getBottomLeft() { return bottomLeft; }
        public void setBottomLeft(CardPayload bottomLeft) { this.bottomLeft = bottomLeft; }
        public CardPayload getBottomRight() { return bottomRight; }
        public void setBottomRight(CardPayload bottomRight) { this.bottomRight = bottomRight; }
    }

    // Getters and Setters for GameStatePayload
    public boolean isAllHere() { return allHere; }
    public void setAllHere(boolean allHere) { this.allHere = allHere; }
    public int getDiscardSize() { return discardSize; }
    public void setDiscardSize(int discardSize) { this.discardSize = discardSize; }
    public int getDrawSize() { return drawSize; }
    public void setDrawSize(int drawSize) { this.drawSize = drawSize; }
    public String getGameId() { return gameId; }
    public void setGameId(String gameId) { this.gameId = gameId; }
    public String getVersion() { return version; }
    public void setVersion(String version) { this.version = version; }
    public boolean isGameStarted() { return gameStarted; }
    public void setGameStarted(boolean gameStarted) { this.gameStarted = gameStarted; }
    public boolean isGameOver() { return gameOver; }
    public void setGameOver(boolean gameOver) { this.gameOver = gameOver; }
    public String getKnocker() { return knocker; }
    public void setKnocker(String knocker) { this.knocker = knocker; }
    public VisibleHandPayload getHand() { return hand; }
    public void setHand(VisibleHandPayload hand) { this.hand = hand; }
    public int getNumberOfPlayers() { return numberOfPlayers; }
    public void setNumberOfPlayers(int numberOfPlayers) { this.numberOfPlayers = numberOfPlayers; }
    public List<String> getPlayers() { return players; }
    public void setPlayers(List<String> players) { this.players = players; }
    public List<Integer> getScores() { return scores; }
    public void setScores(List<Integer> scores) { this.scores = scores; }
    public CardPayload getTopDiscard() { return topDiscard; }
    public void setTopDiscard(CardPayload topDiscard) { this.topDiscard = topDiscard; }
    public CardPayload getTopDraw() { return topDraw; }
    public void setTopDraw(CardPayload topDraw) { this.topDraw = topDraw; }
    public boolean isYourTurn() { return yourTurn; }
    public void setYourTurn(boolean yourTurn) { this.yourTurn = yourTurn; }
    public String getCurrentTurnPlayerId() { return currentTurnPlayerId; }
    public void setCurrentTurnPlayerId(String currentTurnPlayerId) { this.currentTurnPlayerId = currentTurnPlayerId; }
}
