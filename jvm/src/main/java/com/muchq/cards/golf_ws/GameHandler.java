package com.muchq.cards.golf_ws;

import com.muchq.json.JsonUtils;
import java.io.IOException;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import org.eclipse.jetty.websocket.api.Session;
import org.eclipse.jetty.websocket.api.annotations.OnWebSocketClose;
import org.eclipse.jetty.websocket.api.annotations.OnWebSocketConnect;
import org.eclipse.jetty.websocket.api.annotations.OnWebSocketMessage;
import org.eclipse.jetty.websocket.api.annotations.WebSocket;

@WebSocket
public class GameHandler {
    private static final Set<Session> sessions = Collections.synchronizedSet(new HashSet<>());
    private static final Map<String, Game> games = new HashMap<>();

    @OnWebSocketConnect
    public void onConnect(Session session) {
        sessions.add(session);
    }

    @OnWebSocketClose
    public void onClose(Session session, int statusCode, String reason) {
        sessions.remove(session);
    }

    @OnWebSocketMessage
    public void onMessage(Session session, String message) {
        Request request = JsonUtils.readAs(message, Request.class);
        String action = request.getAction();
        switch (action) {
            case "start-new-game":
                startNewGame(session, request);
                break;
            case "join-existing-game":
                joinExistingGame(session, request);
                break;
            case "peek":
                peek(session, request);
                break;
            case "swap":
                swap(session, request);
                break;
            case "knock":
                knock(session, request);
                break;
            default:
                sendError(session, "Invalid action");
        }
    }

    private void startNewGame(Session session, Request request) {
        String gameId = request.getGameId();
        Game game = new Game(gameId);
        games.put(gameId, game);
        sendResponse(session, "Game started with ID: " + gameId);
    }

    private void joinExistingGame(Session session, Request request) {
        String gameId = request.getGameId();
        Game game = games.get(gameId);
        if (game != null) {
            game.addPlayer(session);
            sendResponse(session, "Joined game with ID: " + gameId);
        } else {
            sendError(session, "Game not found");
        }
    }

    private void peek(Session session, Request request) {
        // Implement peek logic
        sendResponse(session, "Peek action executed");
    }

    private void swap(Session session, Request request) {
        // Implement swap logic
        sendResponse(session, "Swap action executed");
    }

    private void knock(Session session, Request request) {
        // Implement knock logic
        sendResponse(session, "Knock action executed");
    }

    private void sendResponse(Session session, String message) {
        try {
            Response response = Response.success(message);
            session.getRemote().sendString(JsonUtils.writeAsString(response));
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    private void sendError(Session session, String error) {
        try {
            Response response = Response.error(error);
            session.getRemote().sendString(JsonUtils.writeAsString(response));
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
}

