package com.muchq.games.one_d4.engine;

import com.muchq.games.one_d4.engine.model.AttackOccurrence;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.ParsedGame;
import com.muchq.games.one_d4.engine.model.PositionContext;
import com.muchq.games.one_d4.motifs.AttackOccurrenceDetector;
import com.muchq.games.one_d4.motifs.MotifDetector;
import java.util.EnumMap;
import java.util.EnumSet;
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
  private final AttackOccurrenceDetector attackDetector;

  public FeatureExtractor(
      PgnParser pgnParser, GameReplayer replayer, List<MotifDetector> detectors) {
    this.pgnParser = pgnParser;
    this.replayer = replayer;
    this.detectors = detectors;
    this.attackDetector = new AttackOccurrenceDetector();
  }

  public GameFeatures extract(String pgn) {
    ParsedGame parsed = pgnParser.parse(pgn);
    List<PositionContext> positions;
    try {
      positions = replayer.replay(parsed.moveText());
    } catch (Exception e) {
      LOG.warn("Failed to replay game, skipping motif detection", e);
      return new GameFeatures(EnumSet.noneOf(Motif.class), 0, Map.of(), List.of());
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

    List<AttackOccurrence> attackOccurrences = List.of();
    try {
      attackOccurrences = attackDetector.detect(positions);
    } catch (Exception e) {
      LOG.warn("Attack occurrence detector failed", e);
    }

    return new GameFeatures(foundMotifs, numMoves, allOccurrences, attackOccurrences);
  }
}
