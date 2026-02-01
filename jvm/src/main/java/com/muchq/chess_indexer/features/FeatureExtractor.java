package com.muchq.chess_indexer.features;

import com.muchq.chess_indexer.model.GameFeatures;
import com.muchq.chess_indexer.pgn.PgnGame;
import com.muchq.chess_indexer.pgn.PgnParser;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

public class FeatureExtractor {

  private final PgnParser parser = new PgnParser();

  public FeatureExtractionResult extract(UUID gameId, String pgn) {
    PgnGame game = parser.parse(pgn);
    List<String> moves = game.moves();

    boolean hasCastle = false;
    boolean hasPromotion = false;
    boolean hasCheck = false;
    boolean hasCheckmate = false;

    List<Motif> motifs = new ArrayList<>();

    int ply = 0;
    int castleFirst = -1;
    int promotionFirst = -1;
    int checkFirst = -1;
    int checkmateFirst = -1;

    for (String move : moves) {
      ply++;
      if (castleFirst == -1 && isCastle(move)) {
        castleFirst = ply;
        hasCastle = true;
      }
      if (promotionFirst == -1 && move.contains("=")) {
        promotionFirst = ply;
        hasPromotion = true;
      }
      if (checkmateFirst == -1 && move.contains("#")) {
        checkmateFirst = ply;
        hasCheckmate = true;
      }
      if (checkFirst == -1 && move.contains("+")) {
        checkFirst = ply;
        hasCheck = true;
      }
    }

    if (castleFirst != -1) {
      motifs.add(new Motif("castle", castleFirst));
    }
    if (promotionFirst != -1) {
      motifs.add(new Motif("promotion", promotionFirst));
    }
    if (checkFirst != -1) {
      motifs.add(new Motif("check", checkFirst));
    }
    if (checkmateFirst != -1) {
      motifs.add(new Motif("checkmate", checkmateFirst));
    }

    GameFeatures features = new GameFeatures(
        gameId,
        moves.size(),
        hasCastle,
        hasPromotion,
        hasCheck,
        hasCheckmate
    );

    return new FeatureExtractionResult(features, motifs);
  }

  private boolean isCastle(String move) {
    return move.startsWith("O-O-O") || move.startsWith("O-O");
  }
}
