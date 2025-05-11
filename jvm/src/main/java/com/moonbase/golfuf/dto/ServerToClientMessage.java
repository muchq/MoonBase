package com.moonbase.golfuf.dto;

public class ServerToClientMessage {
    private String type;
    private WebSocketPayload payload;

    public ServerToClientMessage(String type, WebSocketPayload payload) {
        this.type = type;
        this.payload = payload;
    }

    // Getters
    public String getType() {
        return type;
    }

    public WebSocketPayload getPayload() {
        return payload;
    }
}
