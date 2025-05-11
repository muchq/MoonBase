package com.moonbase.golfuf.dto;

public class RegisterUserPayload implements WebSocketPayload {
    private String userId;

    // Getters and setters
    public String getUserId() {
        return userId;
    }

    public void setUserId(String userId) {
        this.userId = userId;
    }
}
