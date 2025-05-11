package com.moonbase.golfuf.dto;

// For actions like PEEK, DISCARD_DRAW, SWAP_FOR_DRAW, SWAP_FOR_DISCARD, KNOCK
public class PlayerActionPayload implements WebSocketPayload {
    private String userId;
    private String gameId;
    private String cardPosition; // e.g., "TOP_LEFT", for swap actions. Can be optional.

    // Getters and setters
    public String getUserId() {
        return userId;
    }

    public void setUserId(String userId) {
        this.userId = userId;
    }

    public String getGameId() {
        return gameId;
    }

    public void setGameId(String gameId) {
        this.gameId = gameId;
    }

    public String getCardPosition() {
        return cardPosition;
    }

    public void setCardPosition(String cardPosition) {
        this.cardPosition = cardPosition;
    }
}
