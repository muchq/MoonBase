package com.moonbase.smithy.runtime;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Represents a WebSocket session/connection.
 */
public interface WebSocketSession {

    /**
     * Gets the unique session ID.
     */
    String getId();

    /**
     * Sends a message to this session.
     */
    void send(WebSocketMessage message);

    /**
     * Sends a raw text message to this session.
     */
    void sendText(String text);

    /**
     * Closes the session.
     */
    void close();

    /**
     * Closes the session with a reason.
     */
    void close(int code, String reason);

    /**
     * Checks if the session is open.
     */
    boolean isOpen();

    /**
     * Gets session attributes for storing user-specific data.
     */
    Map<String, Object> getAttributes();

    /**
     * Gets a session attribute.
     */
    @SuppressWarnings("unchecked")
    default <T> T getAttribute(String key) {
        return (T) getAttributes().get(key);
    }

    /**
     * Sets a session attribute.
     */
    default void setAttribute(String key, Object value) {
        getAttributes().put(key, value);
    }

    /**
     * Removes a session attribute.
     */
    default void removeAttribute(String key) {
        getAttributes().remove(key);
    }

    /**
     * Gets the remote address of the client.
     */
    String getRemoteAddress();

    /**
     * Creates a simple in-memory session for testing.
     */
    static WebSocketSession createTestSession(String id) {
        return new WebSocketSession() {
            private final Map<String, Object> attributes = new ConcurrentHashMap<>();
            private boolean open = true;

            @Override
            public String getId() {
                return id;
            }

            @Override
            public void send(WebSocketMessage message) {
                System.out.println("[WS:" + id + "] " + message.getAction() + ": " + message.getPayload());
            }

            @Override
            public void sendText(String text) {
                System.out.println("[WS:" + id + "] " + text);
            }

            @Override
            public void close() {
                open = false;
            }

            @Override
            public void close(int code, String reason) {
                open = false;
            }

            @Override
            public boolean isOpen() {
                return open;
            }

            @Override
            public Map<String, Object> getAttributes() {
                return attributes;
            }

            @Override
            public String getRemoteAddress() {
                return "127.0.0.1";
            }
        };
    }
}
