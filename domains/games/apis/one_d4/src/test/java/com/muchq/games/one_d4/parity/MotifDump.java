package com.muchq.games.one_d4.parity;

import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.GameReplayer;
import com.muchq.games.one_d4.engine.PgnParser;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.motifs.Detectors;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;

/**
 * Renders what the Java pipeline extracts from a PGN bank, as sorted TSV.
 *
 * <p>This is the oracle the C++ port is measured against. It is a library so that the golden file
 * and the test that checks the golden is still current come from the same code — a generator the
 * checker does not share is a golden that drifts.
 *
 * <p>Rows are sorted rather than emitted in detector order: order is pinned on the C++ side by its
 * own tests, and leaving it in here would make every comparison a comparison of two orderings.
 */
public final class MotifDump {

  private MotifDump() {}

  /** One row per occurrence; one REPLAY_FAILED row per game the pipeline could not play. */
  public static String dump(List<String> games) {
    FeatureExtractor extractor =
        new FeatureExtractor(new PgnParser(), new GameReplayer(), Detectors.defaultDetectors());

    List<String> rows = new ArrayList<>();
    for (int i = 0; i < games.size(); i++) {
      GameFeatures features;
      try {
        features = extractor.extractOrThrow(games.get(i));
      } catch (RuntimeException e) {
        rows.add(i + "\tREPLAY_FAILED");
        continue;
      }
      for (Motif motif : Motif.values()) {
        for (GameFeatures.MotifOccurrence occ :
            features.occurrences().getOrDefault(motif, List.of())) {
          rows.add(row(i, motif, occ));
        }
      }
    }
    rows.sort(Comparator.naturalOrder());
    return String.join("\n", rows) + "\n";
  }

  /** Splits a concatenated PGN bank the way the corpus tests do. */
  public static List<String> split(String pgn) {
    return Arrays.stream(pgn.split("(?m)(?=^\\[Event \")")).filter(g -> !g.isBlank()).toList();
  }

  private static String row(int game, Motif motif, GameFeatures.MotifOccurrence occ) {
    return String.join(
        "\t",
        String.format("%04d", game),
        motif.name(),
        String.format("%04d", occ.ply()),
        occ.side(),
        String.valueOf(occ.moveNumber()),
        occ.description(),
        or(occ.attacker()),
        or(occ.target()),
        or(occ.movedPiece()),
        occ.isDiscovered() ? "1" : "0",
        occ.isMate() ? "1" : "0",
        or(occ.pinType()));
  }

  private static String or(String value) {
    return value == null ? "-" : value;
  }
}
