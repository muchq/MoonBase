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

  private final PgnParser pgnParser;
  private final GameReplayer replayer;
  private final List<MotifDetector> detectors;

  public FeatureExtractor(
      PgnParser pgnParser, GameReplayer replayer, List<MotifDetector> detectors) {
    this.pgnParser = pgnParser;
    this.replayer = replayer;
    this.detectors = detectors;
  }

  public GameFeatures extract(String pgn) {
    ParsedGame parsed = pgnParser.parse(pgn);
    List<PositionContext> positions;
    try {
      positions = replayer.replay(parsed.moveText());
    } catch (Exception e) {
      LOG.warn("Failed to replay game, skipping motif detection", e);
      return new GameFeatures(EnumSet.noneOf(Motif.class), 0, Map.of());
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

    // Post-process: derive FORK occurrences from ATTACK occurrences.
    // A fork is when the same attacker at the same ply attacks 2+ targets.
    deriveForkFromAttack(allOccurrences, foundMotifs);

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
