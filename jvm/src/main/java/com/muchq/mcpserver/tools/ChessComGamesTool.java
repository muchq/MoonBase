package com.muchq.mcpserver.tools;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.chess_com_api.ChessClient;

import java.io.IOException;
import java.time.YearMonth;
import java.util.List;
import java.util.Map;

public class ChessComGamesTool implements McpTool {

    private final ChessClient chessClient;
    private final ObjectMapper mapper;

    public ChessComGamesTool(ChessClient chessClient, ObjectMapper mapper) {
        this.chessClient = chessClient;
        this.mapper = mapper;
    }

    @Override
    public String getName() {
        return "chess_com_games";
    }

    @Override
    public String getDescription() {
        return "Returns the requested user's chess.com games for the specified month and year. For example, username: hikaru, year: 2025, month: 01";
    }

    @Override
    public Map<String, Object> getInputSchema() {
        return Map.of(
                "type", "object",
                "properties", Map.of(
                        "username", Map.of("type", "string", "description", "The player's chess.com username"),
                        "year", Map.of("type", "string", "description", "The year the games were played (yyyy format)"),
                        "month", Map.of("type", "string", "description", "The month the games were played (MM format)")
                        ),
                "required", List.of("username", "year", "month")
        );
    }

    @Override
    public String execute(Map<String, Object> arguments) {
        String player = (String) arguments.get("username");
        String yearStr = (String) arguments.get("year");
        String monthStr = (String) arguments.get("month");
        int year = Integer.parseInt(yearStr);
        int month = Integer.parseInt(monthStr);
        var gamesMaybe = chessClient.fetchGames(player, YearMonth.of(year, month));

        if (gamesMaybe.isEmpty()) {
            return "player not found";
        }

        try {
            return mapper.writeValueAsString(gamesMaybe.get());
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }
}
