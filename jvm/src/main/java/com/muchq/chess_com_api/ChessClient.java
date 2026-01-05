package com.muchq.chess_com_api;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.json.JsonUtils;

import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.util.Optional;

public class ChessClient {
    private static final String BASE_URL = "https://api.chess.com/pub/player";

    private final HttpClient httpClient;
    private final ObjectMapper mapper;

    public ChessClient(HttpClient httpClient, ObjectMapper objectMapper) {
        this.httpClient = httpClient;
        this.mapper = objectMapper;
    }

    public Optional<StatsResponse> fetchStats(String player) {
        String url = BASE_URL + "/" + player + "/stats";

        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(url))
                .GET()
                .build();

        HttpResponse<String> response = null;
        try {
            response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
        } catch (IOException e) {
            throw new RuntimeException(e);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new RuntimeException(e);
        }

        if (response.statusCode() == 404) {
            return Optional.empty();
        }

        try {
            return Optional.of(mapper.readValue(response.body(), StatsResponse.class));
        } catch (JsonProcessingException e) {
            throw new RuntimeException(e);
        }
    }

    public static void main(String[] args) {
        ObjectMapper mapper = JsonUtils.mapper();
        HttpClient client = HttpClient.newHttpClient();

        var chessClient = new ChessClient(client, mapper);
        var stats = chessClient.fetchStats("drawlya");
        System.out.println(stats);
    }
}
