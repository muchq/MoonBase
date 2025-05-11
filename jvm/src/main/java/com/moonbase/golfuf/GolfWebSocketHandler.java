package com.moonbase.golfuf;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JavaType;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.util.concurrent.FutureCallback;
import com.google.common.util.concurrent.Futures;
import com.google.common.util.concurrent.MoreExecutors;
import com.moonbase.golfuf.dto.*;
import golf_grpc.GolfOuterClass;
import org.checkerframework.checker.nullness.qual.Nullable;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;
import org.springframework.web.socket.CloseStatus;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.TextWebSocketHandler;

import java.io.IOException;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArraySet;

@Component
public class GolfWebSocketHandler extends TextWebSocketHandler {

    private static final Logger logger = LoggerFactory.getLogger(GolfWebSocketHandler.class);
    private final GolfGrpcClientService golfGrpcClientService;
    private final ObjectMapper objectMapper = new ObjectMapper();

    // Session and game management
    private final Map<WebSocketSession, String> sessionToUserId = new ConcurrentHashMap<>();
    private final Map<String, CopyOnWriteArraySet<WebSocketSession>> gameIdToSessions = new ConcurrentHashMap<>();

    // Constants for message types
    private static final String TYPE_REGISTER_USER = "REGISTER_USER";
    private static final String TYPE_NEW_GAME = "NEW_GAME";
    private static final String TYPE_JOIN_GAME = "JOIN_GAME";
    private static final String TYPE_PEEK = "PEEK";
    private static final String TYPE_DISCARD_DRAW = "DISCARD_DRAW";
    private static final String TYPE_SWAP_FOR_DRAW = "SWAP_FOR_DRAW";
    private static final String TYPE_SWAP_FOR_DISCARD = "SWAP_FOR_DISCARD";
    private static final String TYPE_KNOCK = "KNOCK";

    private static final String TYPE_GAME_STATE_UPDATE = "GAME_STATE_UPDATE";
    private static final String TYPE_ERROR = "ERROR";
    private static final String TYPE_USER_REGISTERED = "USER_REGISTERED";


    public GolfWebSocketHandler(GolfGrpcClientService golfGrpcClientService) {
        this.golfGrpcClientService = golfGrpcClientService;
    }

    @Override
    public void afterConnectionEstablished(WebSocketSession session) throws Exception {
        logger.info("New WebSocket connection established: {}", session.getId());
    }

    @Override
    protected void handleTextMessage(WebSocketSession session, TextMessage message) throws Exception {
        String payload = message.getPayload();
        logger.info("Received message from {}: {}", session.getId(), payload);

        try {
            ClientToServerMessage genericMessage = objectMapper.readValue(payload, ClientToServerMessage.class);

            switch (genericMessage.getType()) {
                case TYPE_REGISTER_USER:
                    handleRegisterUser(session, objectMapper.readValue(payload, getTypeFactory(RegisterUserPayload.class)));
                    break;
                case TYPE_NEW_GAME:
                    handleNewGame(session, objectMapper.readValue(payload, getTypeFactory(NewGamePayload.class)));
                    break;
                case TYPE_JOIN_GAME:
                    handleJoinGame(session, objectMapper.readValue(payload, getTypeFactory(JoinGamePayload.class)));
                    break;
                case TYPE_PEEK:
                    handlePeek(session, objectMapper.readValue(payload, getTypeFactory(PlayerActionPayload.class)));
                    break;
                case TYPE_DISCARD_DRAW:
                    handleDiscardDraw(session, objectMapper.readValue(payload, getTypeFactory(PlayerActionPayload.class)));
                    break;
                case TYPE_SWAP_FOR_DRAW:
                    handleSwapForDraw(session, objectMapper.readValue(payload, getTypeFactory(PlayerActionPayload.class)));
                    break;
                case TYPE_SWAP_FOR_DISCARD:
                case TYPE_KNOCK:
                    // TODO: Implement handlers for these game actions
                    handlePlayerAction(session, genericMessage.getType(), objectMapper.readValue(payload, getTypeFactory(PlayerActionPayload.class)));
                    break;
                default:
                    sendErrorMessage(session, "Unknown action type: " + genericMessage.getType(), genericMessage.getType());
            }
        } catch (JsonProcessingException e) {
            logger.error("Error processing JSON message from {}: {}", session.getId(), payload, e);
            sendErrorMessage(session, "Invalid JSON message format.", null);
        } catch (Exception e) {
            logger.error("Error handling message from {}: {}", session.getId(), payload, e);
            sendErrorMessage(session, "Internal server error while handling message.", null);
        }
    }

    private <T extends WebSocketPayload> JavaType getTypeFactory(Class<T> payloadClass) {
        return objectMapper.getTypeFactory().constructParametricType(ClientToServerMessage.class, payloadClass);
    }

    private void handleRegisterUser(WebSocketSession session, ClientToServerMessage clientMessage) {
        RegisterUserPayload payload = (RegisterUserPayload) clientMessage.getPayload();
        String userId = payload.getUserId();
        if (userId == null || userId.trim().isEmpty()) {
            sendErrorMessage(session, "User ID cannot be empty.", TYPE_REGISTER_USER);
            return;
        }

        WebSocketSession oldSession = sessionToUserId.entrySet().stream()
            .filter(entry -> userId.equals(entry.getValue()))
            .map(Map.Entry::getKey)
            .findFirst().orElse(null);
        if (oldSession != null) {
            sessionToUserId.remove(oldSession);
            logger.info("User {} re-registered, removing old session {}", userId, oldSession.getId());
        }

        sessionToUserId.put(session, userId);
        logger.info("User {} registered with session {}", userId, session.getId());

        Futures.addCallback(
                golfGrpcClientService.registerUser(userId),
                new FutureCallback<GolfOuterClass.RegisterUserResponse>() {
                    @Override
                    public void onSuccess(@Nullable GolfOuterClass.RegisterUserResponse result) {
                        sendMessageToSession(session, new ServerToClientMessage(TYPE_USER_REGISTERED, new SimpleMessagePayload("User " + userId + " registered successfully.")));
                    }

                    @Override
                    public void onFailure(Throwable t) {
                        logger.error("gRPC RegisterUser failed for user {}: {}", userId, t.getMessage());
                        sendErrorMessage(session, "Failed to register user with game server: " + t.getMessage(), TYPE_REGISTER_USER);
                        sessionToUserId.remove(session);
                    }
                },
                MoreExecutors.directExecutor()
        );
    }

    private void handleNewGame(WebSocketSession session, ClientToServerMessage clientMessage) {
        NewGamePayload payload = (NewGamePayload) clientMessage.getPayload();
        String userId = payload.getUserId();
        if (!isUserSessionValid(session, userId, TYPE_NEW_GAME)) return;

        Futures.addCallback(
                golfGrpcClientService.newGame(userId, payload.getNumberOfPlayers()),
                new FutureCallback<GolfOuterClass.NewGameResponse>() {
                    @Override
                    public void onSuccess(@Nullable GolfOuterClass.NewGameResponse result) {
                        if (result == null || !result.hasGameState()) {
                            sendErrorMessage(session, "New game creation returned no game state.", TYPE_NEW_GAME);
                            return;
                        }
                        GolfOuterClass.GameState grpcGameState = result.getGameState();
                        String gameId = grpcGameState.getGameId();

                        gameIdToSessions.computeIfAbsent(gameId, k -> new CopyOnWriteArraySet<>()).add(session);
                        session.getAttributes().put("gameId", gameId);

                        broadcastGameState(gameId, grpcGameState);
                    }

                    @Override
                    public void onFailure(Throwable t) {
                        logger.error("gRPC NewGame failed for user {}: {}", userId, t.getMessage());
                        sendErrorMessage(session, "Failed to create new game: " + t.getMessage(), TYPE_NEW_GAME);
                    }
                },
                MoreExecutors.directExecutor()
        );
    }

    private void handleJoinGame(WebSocketSession session, ClientToServerMessage clientMessage) {
        JoinGamePayload payload = (JoinGamePayload) clientMessage.getPayload();
        String userId = payload.getUserId();
        String gameId = payload.getGameId();
        if (!isUserSessionValid(session, userId, TYPE_JOIN_GAME)) return;

        Futures.addCallback(
                golfGrpcClientService.joinGame(userId, gameId),
                new FutureCallback<GolfOuterClass.JoinGameResponse>() {
                    @Override
                    public void onSuccess(@Nullable GolfOuterClass.JoinGameResponse result) {
                        if (result == null || !result.hasGameState()) {
                            sendErrorMessage(session, "Join game returned no game state.", TYPE_JOIN_GAME);
                            return;
                        }
                        GolfOuterClass.GameState grpcGameState = result.getGameState();

                        gameIdToSessions.computeIfAbsent(gameId, k -> new CopyOnWriteArraySet<>()).add(session);
                        session.getAttributes().put("gameId", gameId);

                        broadcastGameState(gameId, grpcGameState);
                    }

                    @Override
                    public void onFailure(Throwable t) {
                        logger.error("gRPC JoinGame failed for user {} game {}: {}", userId, gameId, t.getMessage());
                        sendErrorMessage(session, "Failed to join game: " + t.getMessage(), TYPE_JOIN_GAME);
                    }
                },
                MoreExecutors.directExecutor()
        );
    }

    private void handlePeek(WebSocketSession session, ClientToServerMessage clientMessage) {
        PlayerActionPayload payload = (PlayerActionPayload) clientMessage.getPayload();
        String userId = payload.getUserId();
        String gameId = payload.getGameId();
        if (!isUserSessionValid(session, userId, TYPE_PEEK)) return;

        Futures.addCallback(
            golfGrpcClientService.peek(userId, gameId),
            new FutureCallback<GolfOuterClass.PeekResponse>() {
                @Override
                public void onSuccess(@Nullable GolfOuterClass.PeekResponse result) {
                    if (result == null || !result.hasGameState()) {
                        sendErrorMessage(session, "Peek action returned no game state.", TYPE_PEEK);
                        return;
                    }
                    broadcastGameState(gameId, result.getGameState());
                }

                @Override
                public void onFailure(Throwable t) {
                    logger.error("gRPC Peek failed for user {} game {}: {}", userId, gameId, t.getMessage());
                    sendErrorMessage(session, "Failed to peek: " + t.getMessage(), TYPE_PEEK);
                }
            },
            MoreExecutors.directExecutor()
        );
    }

    private void handleDiscardDraw(WebSocketSession session, ClientToServerMessage clientMessage) {
        PlayerActionPayload payload = (PlayerActionPayload) clientMessage.getPayload();
        String userId = payload.getUserId();
        String gameId = payload.getGameId();
        if (!isUserSessionValid(session, userId, TYPE_DISCARD_DRAW)) return;

        Futures.addCallback(
            golfGrpcClientService.discardDraw(userId, gameId),
            new FutureCallback<GolfOuterClass.DiscardDrawResponse>() {
                @Override
                public void onSuccess(@Nullable GolfOuterClass.DiscardDrawResponse result) {
                    if (result == null || !result.hasGameState()) {
                        sendErrorMessage(session, "Discard action returned no game state.", TYPE_DISCARD_DRAW);
                        return;
                    }
                    broadcastGameState(gameId, result.getGameState());
                }

                @Override
                public void onFailure(Throwable t) {
                    logger.error("gRPC DiscardDraw failed for user {} game {}: {}", userId, gameId, t.getMessage());
                    sendErrorMessage(session, "Failed to discard draw: " + t.getMessage(), TYPE_DISCARD_DRAW);
                }
            },
            MoreExecutors.directExecutor()
        );
    }

    private void handleSwapForDraw(WebSocketSession session, ClientToServerMessage clientMessage) {
        PlayerActionPayload payload = (PlayerActionPayload) clientMessage.getPayload();
        String userId = payload.getUserId();
        String gameId = payload.getGameId();
        String positionStr = payload.getCardPosition(); // e.g., "TOP_LEFT"

        if (!isUserSessionValid(session, userId, TYPE_SWAP_FOR_DRAW)) return;

        if (positionStr == null) {
            sendErrorMessage(session, "Card position to swap is required.", TYPE_SWAP_FOR_DRAW);
            return;
        }

        GolfOuterClass.Position grpcPosition;
        try {
            grpcPosition = GolfOuterClass.Position.valueOf(positionStr.toUpperCase());
        } catch (IllegalArgumentException e) {
            sendErrorMessage(session, "Invalid card position: " + positionStr, TYPE_SWAP_FOR_DRAW);
            return;
        }

        Futures.addCallback(
            golfGrpcClientService.swapForDraw(userId, gameId, grpcPosition),
            new FutureCallback<GolfOuterClass.SwapForDrawResponse>() {
                @Override
                public void onSuccess(@Nullable GolfOuterClass.SwapForDrawResponse result) {
                    if (result == null || !result.hasGameState()) {
                        sendErrorMessage(session, "Swap action returned no game state.", TYPE_SWAP_FOR_DRAW);
                        return;
                    }
                    broadcastGameState(gameId, result.getGameState());
                }

                @Override
                public void onFailure(Throwable t) {
                    logger.error("gRPC SwapForDraw failed for user {} game {}: {}", userId, gameId, t.getMessage());
                    sendErrorMessage(session, "Failed to swap for draw: " + t.getMessage(), TYPE_SWAP_FOR_DRAW);
                }
            },
            MoreExecutors.directExecutor()
        );
    }

    // Placeholder for actual game action handlers
    private void handlePlayerAction(WebSocketSession session, String actionType, ClientToServerMessage clientMessage) {
        PlayerActionPayload payload = (PlayerActionPayload) clientMessage.getPayload();
        String userId = payload.getUserId();
        String gameId = payload.getGameId();

        if (!isUserSessionValid(session, userId, actionType)) return;

        logger.info("Received player action: {} from user {} for game {}", actionType, userId, gameId);
        // In a real implementation, you'd call the specific gRPC service method based on actionType
        // For example:
        // switch (actionType) {
        //     case TYPE_PEEK: golfGrpcClientService.peek(userId, gameId); break;
        //     ...
        // }
        // And then handle the FutureCallback to broadcast game state.
        sendErrorMessage(session, "Action " + actionType + " not fully implemented yet.", actionType);
    }

    private boolean isUserSessionValid(WebSocketSession session, String userIdFromPayload, String actionType) {
        String userIdFromSession = sessionToUserId.get(session);
        if (userIdFromSession == null) {
            sendErrorMessage(session, "User not registered or session expired. Please register first.", actionType);
            return false;
        }
        if (!userIdFromSession.equals(userIdFromPayload)) {
            sendErrorMessage(session, "User ID in payload does not match registered user for this session.", actionType);
            return false;
        }
        return true;
    }

    private GameStatePayload convertToGameStatePayload(GolfOuterClass.GameState grpcState, String recipientUserId) {
        GameStatePayload payload = new GameStatePayload();
        payload.setAllHere(grpcState.getAllHere());
        payload.setDiscardSize(grpcState.getDiscardSize());
        payload.setDrawSize(grpcState.getDrawSize());
        payload.setGameId(grpcState.getGameId());
        payload.setVersion(grpcState.getVersion());
        payload.setGameStarted(grpcState.getGameStarted());
        payload.setGameOver(grpcState.getGameOver());
        if (grpcState.hasKnocker()) {
            payload.setKnocker(grpcState.getKnocker());
        }

        String currentTurnPlayerId = grpcState.getCurrentPlayerId();
        payload.setCurrentTurnPlayerId(currentTurnPlayerId);
        payload.setYourTurn(recipientUserId.equals(currentTurnPlayerId));

        // Hand is player-specific, should only be sent if it's this recipient's hand AND their turn (or initial peek).
        // The gRPC service should only populate `hand` if it's meant for the user who made the gRPC call.
        // `grpcState.getYourTurn()` indicates if the `hand` and `topDraw` fields are relevant for the original gRPC caller.
        if (grpcState.hasHand() && grpcState.getYourTurn() && recipientUserId.equals(currentTurnPlayerId)) {
            // This implies the recipient is the one for whom the gRPC yourTurn was true and hand was populated.
            GolfOuterClass.VisibleHand grpcHand = grpcState.getHand();
            GameStatePayload.VisibleHandPayload handPayload = new GameStatePayload.VisibleHandPayload();
            if (grpcHand.hasBottomLeft()) handPayload.setBottomLeft(convertToCardPayload(grpcHand.getBottomLeft()));
            if (grpcHand.hasBottomRight()) handPayload.setBottomRight(convertToCardPayload(grpcHand.getBottomRight()));
            payload.setHand(handPayload);
        }

        payload.setNumberOfPlayers(grpcState.getNumberOfPlayers());
        payload.setPlayers(grpcState.getPlayersList());
        payload.setScores(grpcState.getScoresList());
        if (grpcState.hasTopDiscard()) {
            payload.setTopDiscard(convertToCardPayload(grpcState.getTopDiscard()));
        }

        // TopDraw is specific to the player whose turn it is AND who has just drawn.
        // `grpcState.getYourTurn()` is key here for the original gRPC caller.
        if (grpcState.hasTopDraw() && grpcState.getYourTurn() && recipientUserId.equals(currentTurnPlayerId)) {
            payload.setTopDraw(convertToCardPayload(grpcState.getTopDraw()));
        }

        return payload;
    }

    private GameStatePayload.CardPayload convertToCardPayload(cards_proto.Cards.Card grpcCard) {
        return new GameStatePayload.CardPayload(grpcCard.getSuit().name(), grpcCard.getRank().name());
    }

    private void broadcastGameState(String gameId, GolfOuterClass.GameState grpcGameState) {
        Set<WebSocketSession> sessionsInGame = gameIdToSessions.get(gameId);
        if (sessionsInGame != null) {
            for (WebSocketSession sessionInGame : sessionsInGame) {
                String recipientUserId = sessionToUserId.get(sessionInGame);
                if (recipientUserId != null) {
                    GameStatePayload clientState = convertToGameStatePayload(grpcGameState, recipientUserId);
                    sendMessageToSession(sessionInGame, new ServerToClientMessage(TYPE_GAME_STATE_UPDATE, clientState));
                }
            }
        }
    }

    private void sendMessageToSession(WebSocketSession session, ServerToClientMessage message) {
        if (session == null || !session.isOpen()) return;
        try {
            session.sendMessage(new TextMessage(objectMapper.writeValueAsString(message)));
        } catch (IOException e) {
            logger.error("Error sending message to session {}: {}", session.getId(), e.getMessage());
        }
    }

    private void sendErrorMessage(WebSocketSession session, String errorMessage, String originalActionType) {
        ErrorPayload errorPayload = new ErrorPayload(errorMessage, originalActionType);
        sendMessageToSession(session, new ServerToClientMessage(TYPE_ERROR, errorPayload));
    }

    @Override
    public void afterConnectionClosed(WebSocketSession session, CloseStatus status) throws Exception {
        String userId = sessionToUserId.remove(session);
        String gameId = (String) session.getAttributes().get("gameId");

        if (userId != null) {
            logger.info("User {} disconnected, session {}", userId, session.getId());
        }
        if (gameId != null) {
            CopyOnWriteArraySet<WebSocketSession> gameSessions = gameIdToSessions.get(gameId);
            if (gameSessions != null) {
                gameSessions.remove(session);
                if (gameSessions.isEmpty()) {
                    gameIdToSessions.remove(gameId);
                    logger.info("Game {} has no more players, removing gameId {}.", gameId, gameId);
                }
                // TODO: Optionally notify other players in the game about the disconnection if game is active.
                // This would involve fetching an updated game state or sending a custom player_left message.
            }
        }
        logger.info("WebSocket connection closed: {} with status: {}", session.getId(), status);
    }

    @Override
    public void handleTransportError(WebSocketSession session, Throwable exception) throws Exception {
        logger.error("WebSocket transport error for session {}: {}", session.getId(), exception.getMessage());
        afterConnectionClosed(session, CloseStatus.PROTOCOL_ERROR);
    }

    static class SimpleMessagePayload implements WebSocketPayload {
        private String message;
        public SimpleMessagePayload(String message) { this.message = message; }
        public String getMessage() { return message; }
        public void setMessage(String message) { this.message = message; }
    }
}
