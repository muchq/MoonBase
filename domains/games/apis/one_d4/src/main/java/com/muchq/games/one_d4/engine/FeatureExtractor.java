package com.muchq.games.one_d4.engine;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.ParsedGame;
import com.muchq.games.one_d4.engine.model.PositionContext;
import com.muchq.games.one_d4.motifs.MotifDetector;
import java.util.ArrayList;
import java.util.EnumMap;
import java.util.EnumSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class FeatureExtractor {
  private static final Logger LOG = LoggerFactory.getLogger(FeatureExtractor.class);

  /**
   * The moves did not replay: an illegal SAN, a move from an impossible position, a PGN that parsed
   * but is not a game. Bad input, not a server fault.
   */
  public static class ReplayFailedException extends IllegalArgumentException {
    public ReplayFailedException(Throwable cause) {
      super("pgn could not be replayed: " + cause.getMessage(), cause);
    }
  }

  private final PgnParser pgnParser;
  private final GameReplayer replayer;
  private final List<MotifDetector> detectors;

  public FeatureExtractor(
      PgnParser pgnParser, GameReplayer replayer, List<MotifDetector> detectors) {
    this.pgnParser = pgnParser;
    this.replayer = replayer;
    this.detectors = detectors;
  }

  /**
   * Extracts features, treating a game that cannot be replayed as one with no motifs.
   *
   * <p>That is what indexing wants: one unplayable game in a month of 400 should not fail the
   * month. It is <em>not</em> what a caller asking about a single game wants — see {@link
   * #extractOrThrow}.
   */
  public GameFeatures extract(String pgn) {
    try {
      return extractOrThrow(pgn);
    } catch (ReplayFailedException e) {
      LOG.warn("Failed to replay game, skipping motif detection", e.getCause());
      return new GameFeatures(EnumSet.noneOf(Motif.class), 0, Map.of());
    }
  }

  /**
   * Extracts features, or fails if the game cannot be replayed.
   *
   * <p>For callers analyzing one game the caller chose. {@link #extract}'s empty result is
   * indistinguishable from a quiet game with no motifs, so an illegal move would come back as a
   * successful "nothing found" — an answer about a game that was never actually played through.
   */
  public GameFeatures extractOrThrow(String pgn) {
    ParsedGame parsed = pgnParser.parse(pgn);
    List<PositionContext> positions;
    try {
      positions = replayer.replay(parsed.moveText());
    } catch (Exception e) {
      throw new ReplayFailedException(e);
    }

    int numMoves = positions.isEmpty() ? 0 : positions.get(positions.size() - 1).moveNumber();
    Set<Motif> foundMotifs = EnumSet.noneOf(Motif.class);
    Map<Motif, List<GameFeatures.MotifOccurrence>> allOccurrences = new EnumMap<>(Motif.class);

    for (MotifDetector detector : detectors) {
      try {
        List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
        if (!occurrences.isEmpty()) {
          foundMotifs.add(detector.motif());
          allOccurrences.put(detector.motif(), occurrences);
        }
      } catch (Exception e) {
        LOG.warn("Motif detector {} failed", detector.motif(), e);
      }
    }

    return new GameFeatures(foundMotifs, numMoves, allOccurrences);
  }

  /**
   * Derives {@link Motif#FORK} occurrences from {@link Motif#ATTACK} occurrences. Groups ATTACK
   * occurrences by (ply, attacker); groups with 2+ targets produce one FORK occurrence per target.
   */
  static void deriveForkFromAttack(
      Map<Motif, List<GameFeatures.MotifOccurrence>> allOccurrences, Set<Motif> foundMotifs) {
    List<GameFeatures.MotifOccurrence> attackOccs =
        allOccurrences.getOrDefault(Motif.ATTACK, List.of());

    // Group by "ply|attacker" — only direct (non-discovered) attacks with a non-null attacker
    Map<String, List<GameFeatures.MotifOccurrence>> groups = new LinkedHashMap<>();
    for (GameFeatures.MotifOccurrence occ : attackOccs) {
      if (occ.attacker() == null || occ.isDiscovered()) continue;
      String key = occ.ply() + "|" + occ.attacker();
      groups.computeIfAbsent(key, k -> new ArrayList<>()).add(occ);
    }

    List<GameFeatures.MotifOccurrence> forkOccs = new ArrayList<>();
    for (List<GameFeatures.MotifOccurrence> group : groups.values()) {
      if (group.size() >= 2) {
        for (GameFeatures.MotifOccurrence attackOcc : group) {
          forkOccs.add(
              GameFeatures.MotifOccurrence.attack(
                  attackOcc.ply(),
                  attackOcc.moveNumber(),
                  attackOcc.side(),
                  "Fork at move " + attackOcc.moveNumber(),
                  attackOcc.movedPiece(),
                  attackOcc.attacker(),
                  attackOcc.target(),
                  false,
                  false));
        }
      }
    }

    if (!forkOccs.isEmpty()) {
      foundMotifs.add(Motif.FORK);
      allOccurrences.put(Motif.FORK, forkOccs);
    }
  }
}
