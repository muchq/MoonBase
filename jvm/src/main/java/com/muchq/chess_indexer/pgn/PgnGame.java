package com.muchq.chess_indexer.pgn;

import java.util.List;
import java.util.Map;

public record PgnGame(Map<String, String> tags, List<String> moves) {}
