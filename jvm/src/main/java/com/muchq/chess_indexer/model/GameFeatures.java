package com.muchq.chess_indexer.model;

import java.util.UUID;

public record GameFeatures(
    UUID gameId,
    int totalPlies,
    boolean hasCastle,
    boolean hasPromotion,
    boolean hasCheck,
    boolean hasCheckmate
) {}
