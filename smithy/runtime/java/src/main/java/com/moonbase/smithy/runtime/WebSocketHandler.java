package com.moonbase.smithy.runtime;

/**
 * Handler interface for WebSocket connections.
 *
 * <p>Implementations handle the lifecycle of WebSocket connections:
 * <ul>
 *   <li>{@link #onConnect} - Called when a new connection is established</li>
 *   <li>{@link #onMessage} - Called when a message is received</li>
 *   <li>{@link #onDisconnect} - Called when a connection is closed</li>
 *   <li>{@link #onError} - Called when an error occurs</li>
 * </ul>
 */
public interface WebSocketHandler {

    /**
     * Called when a new WebSocket connection is established.
     *
     * @param session The WebSocket session
     */
    void onConnect(WebSocketSession session);

    /**
     * Called when a message is received from the client.
     *
     * @param session The WebSocket session
     * @param message The received message
     */
    void onMessage(WebSocketSession session, WebSocketMessage message);

    /**
     * Called when a WebSocket connection is closed.
     *
     * @param session The WebSocket session
     */
    void onDisconnect(WebSocketSession session);

    /**
     * Called when an error occurs on the WebSocket connection.
     *
     * @param session The WebSocket session
     * @param error The error that occurred
     */
    default void onError(WebSocketSession session, Throwable error) {
        // Default implementation logs the error
        System.err.println("WebSocket error for session " + session.getId() + ": " + error.getMessage());
    }

    /**
     * Called to handle a raw text message before parsing.
     * Override this to customize message parsing.
     *
     * @param session The WebSocket session
     * @param text The raw text message
     */
    default void onTextMessage(WebSocketSession session, String text) {
        WebSocketMessage message = WebSocketMessage.fromJson(text);
        onMessage(session, message);
    }

    /**
     * Called to handle a binary message.
     * Default implementation converts to string and calls onTextMessage.
     *
     * @param session The WebSocket session
     * @param data The binary data
     */
    default void onBinaryMessage(WebSocketSession session, byte[] data) {
        onTextMessage(session, new String(data));
    }
}
