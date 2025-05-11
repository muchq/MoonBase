package com.moonbase.golfuf.dto;

public class ErrorPayload implements WebSocketPayload {
    private String message;
    private String originalActionType; // Optional: type of action that caused the error

    public ErrorPayload(String message) {
        this.message = message;
    }

    public ErrorPayload(String message, String originalActionType) {
        this.message = message;
        this.originalActionType = originalActionType;
    }

    // Getters and setters
    public String getMessage() {
        return message;
    }

    public void setMessage(String message) {
        this.message = message;
    }

    public String getOriginalActionType() {
        return originalActionType;
    }

    public void setOriginalActionType(String originalActionType) {
        this.originalActionType = originalActionType;
    }
}
