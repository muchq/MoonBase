package com.muchq.chatserver;

import io.micronaut.websocket.WebSocketBroadcaster;
import io.micronaut.websocket.WebSocketSession;
import io.micronaut.websocket.annotation.OnClose;
import io.micronaut.websocket.annotation.OnMessage;
import io.micronaut.websocket.annotation.OnOpen;
import io.micronaut.websocket.annotation.ServerWebSocket;
import jakarta.inject.Inject;
import java.util.concurrent.ConcurrentHashMap;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@ServerWebSocket("/chat")
public class ChatWebSocket {
  private static final Logger LOG = LoggerFactory.getLogger(ChatWebSocket.class);
  private final ConcurrentHashMap<String, String> users = new ConcurrentHashMap<>();

  @Inject private WebSocketBroadcaster broadcaster;

  @OnOpen
  public void onOpen(WebSocketSession session) {
    String sessionId = session.getId();
    String username = "User-" + sessionId.substring(0, 8);
    users.put(sessionId, username);

    LOG.info("User connected: {} (session: {})", username, sessionId);

    // Notify all users about the new connection
    BroadcastMessage joinMessage =
        new BroadcastMessage("system", username + " joined the chat", users.size());
    broadcaster.broadcastSync(joinMessage);
  }

  @OnMessage
  public void onMessage(ChatMessage message, WebSocketSession session) {
    String sessionId = session.getId();
    String username = users.get(sessionId);

    LOG.info("Message from {}: {}", username, message.text());

    // Broadcast the message to all connected clients
    BroadcastMessage broadcastMessage =
        new BroadcastMessage(username, message.text(), users.size());
    broadcaster.broadcastSync(broadcastMessage);
  }

  @OnClose
  public void onClose(WebSocketSession session) {
    String sessionId = session.getId();
    String username = users.remove(sessionId);

    LOG.info("User disconnected: {} (session: {})", username, sessionId);

    // Notify all users about the disconnection
    BroadcastMessage leaveMessage =
        new BroadcastMessage("system", username + " left the chat", users.size());
    broadcaster.broadcastSync(leaveMessage);
  }
}
