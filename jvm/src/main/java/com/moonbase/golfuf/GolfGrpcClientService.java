package com.moonbase.golfuf;

import golf_grpc.GolfGrpc;
import golf_grpc.GolfOuterClass;
import io.grpc.ManagedChannel;
import io.grpc.ManagedChannelBuilder;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import javax.annotation.PostConstruct;
import javax.annotation.PreDestroy;
import java.util.concurrent.TimeUnit;

@Service
public class GolfGrpcClientService {

    private static final Logger logger = LoggerFactory.getLogger(GolfGrpcClientService.class);

    private ManagedChannel channel;
    private GolfGrpc.GolfFutureStub futureStub; // Or GolfBlockingStub depending on needs

    @Value("${golf.grpc.host:localhost}")
    private String grpcHost;

    @Value("${golf.grpc.port:8088}") // Assuming 8088 based on cpp/golf_grpc/server/README.md example, adjust if needed
    private int grpcPort;

    @PostConstruct
    public void init() {
        logger.info("Initializing gRPC client for Golf service at {}:{}", grpcHost, grpcPort);
        channel = ManagedChannelBuilder.forAddress(grpcHost, grpcPort)
                .usePlaintext() // For development only. Use SSL/TLS in production.
                .build();
        futureStub = GolfGrpc.newFutureStub(channel); // Use newBlockingStub for synchronous calls
        // Alternatively, newStub for async calls with StreamObserver
    }

    // Example method (async, returns ListenableFuture)
    public com.google.common.util.concurrent.ListenableFuture<GolfOuterClass.NewGameResponse> newGame(String userId, int numberOfPlayers) {
        GolfOuterClass.NewGameRequest request = GolfOuterClass.NewGameRequest.newBuilder()
                .setUserId(userId)
                .setNumberOfPlayers(numberOfPlayers)
                .build();
        logger.info("Sending NewGame request for user: {}, players: {}", userId, numberOfPlayers);
        return futureStub.newGame(request);
    }

    public com.google.common.util.concurrent.ListenableFuture<GolfOuterClass.RegisterUserResponse> registerUser(String userId) {
        GolfOuterClass.RegisterUserRequest request = GolfOuterClass.RegisterUserRequest.newBuilder()
            .setUserId(userId)
            .build();
        logger.info("Sending RegisterUser request for user: {}", userId);
        return futureStub.registerUser(request);
    }

    public com.google.common.util.concurrent.ListenableFuture<GolfOuterClass.JoinGameResponse> joinGame(String userId, String gameId) {
        GolfOuterClass.JoinGameRequest request = GolfOuterClass.JoinGameRequest.newBuilder()
                .setUserId(userId)
                .setGameId(gameId)
                .build();
        logger.info("Sending JoinGame request for user: {}, game: {}", userId, gameId);
        return futureStub.joinGame(request);
    }

    public com.google.common.util.concurrent.ListenableFuture<GolfOuterClass.PeekResponse> peek(String userId, String gameId) {
        GolfOuterClass.PeekRequest request = GolfOuterClass.PeekRequest.newBuilder()
                .setUserId(userId)
                .setGameId(gameId)
                .build();
        logger.info("Sending Peek request for user: {}, game: {}", userId, gameId);
        return futureStub.peek(request);
    }

    public com.google.common.util.concurrent.ListenableFuture<GolfOuterClass.DiscardDrawResponse> discardDraw(String userId, String gameId) {
        GolfOuterClass.DiscardDrawRequest request = GolfOuterClass.DiscardDrawRequest.newBuilder()
                .setUserId(userId)
                .setGameId(gameId)
                .build();
        logger.info("Sending DiscardDraw request for user: {}, game: {}", userId, gameId);
        return futureStub.discardDraw(request);
    }

    public com.google.common.util.concurrent.ListenableFuture<GolfOuterClass.SwapForDrawResponse> swapForDraw(String userId, String gameId, GolfOuterClass.Position position) {
        GolfOuterClass.SwapForDrawRequest request = GolfOuterClass.SwapForDrawRequest.newBuilder()
                .setUserId(userId)
                .setGameId(gameId)
                .setPosition(position)
                .build();
        logger.info("Sending SwapForDraw request for user: {}, game: {}, position: {}", userId, gameId, position);
        return futureStub.swapForDraw(request);
    }

    public com.google.common.util.concurrent.ListenableFuture<GolfOuterClass.SwapForDiscardResponse> swapForDiscard(String userId, String gameId, GolfOuterClass.Position position) {
        GolfOuterClass.SwapForDiscardRequest request = GolfOuterClass.SwapForDiscardRequest.newBuilder()
                .setUserId(userId)
                .setGameId(gameId)
                .setPosition(position)
                .build();
        logger.info("Sending SwapForDiscard request for user: {}, game: {}, position: {}", userId, gameId, position);
        return futureStub.swapForDiscard(request);
    }

    public com.google.common.util.concurrent.ListenableFuture<GolfOuterClass.KnockResponse> knock(String userId, String gameId) {
        GolfOuterClass.KnockRequest request = GolfOuterClass.KnockRequest.newBuilder()
                .setUserId(userId)
                .setGameId(gameId)
                .build();
        logger.info("Sending Knock request for user: {}, game: {}", userId, gameId);
        return futureStub.knock(request);
    }

    @PreDestroy
    public void destroy() throws InterruptedException {
        logger.info("Shutting down gRPC client for Golf service");
        if (channel != null) {
            channel.shutdown().awaitTermination(5, TimeUnit.SECONDS);
        }
    }
}
