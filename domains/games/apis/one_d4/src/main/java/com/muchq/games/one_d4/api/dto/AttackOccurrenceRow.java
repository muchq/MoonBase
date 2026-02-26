package com.muchq.games.one_d4.api.dto;

public record AttackOccurrenceRow(
    int moveNumber,
    String side,
    String pieceMoved,
    String attacker,
    String attacked,
    boolean isCheckmate) {}
