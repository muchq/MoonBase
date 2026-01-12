package com.muchq.mcpserver.tools;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.chess_com_api.ChessClient;
import java.io.IOException;
import java.util.List;
import java.util.Map;

public class ChessComStatsTool implements McpTool {

    private final ChessClient chessClient;
    private final ObjectMapper mapper;

    public ChessComStatsTool(ChessClient chessClient, ObjectMapper mapper) {
        this.chessClient = chessClient;
        this.mapper = mapper;
    }

    @Override
    public String getName() {
        return "chess_com_stats";
    }

    @Override
    public String getDescription() {
        return "Returns the requested user's chess.com player stats";
    }

    @Override
    public Map<String, Object> getInputSchema() {
        return Map.of(
                "type", "object",
                "properties", Map.of(
                        "username", Map.of("type", "string", "description", "The player's chess.com username")
                        ),
                "required", List.of("username")
        );
    }

    @Override
    public String execute(Map<String, Object> arguments) {
        String player = (String) arguments.get("username");
        var statsMaybe = chessClient.fetchStats(player);
        if (statsMaybe.isEmpty()) {
            return "player not found";
        }

        try {
            return mapper.writeValueAsString(statsMaybe.get());
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }
}
