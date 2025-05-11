package com.moonbase.golfuf.dto;

public class ClientToServerMessage {
    private String type;
    private WebSocketPayload payload;

    // Getters and setters
    public String getType() {
        return type;
    }

    public void setType(String type) {
        this.type = type;
    }

    public WebSocketPayload getPayload() {
        return payload;
    }

    public void setPayload(WebSocketPayload payload) {
        this.payload = payload;
    }
}
