package com.muchq.games.one_d4.engine;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.motifs.AttackDetector;
import com.muchq.games.one_d4.motifs.MotifDetector;
import java.util.ArrayList;
import java.util.EnumMap;
import java.util.EnumSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.junit.Test;

/**
 * Unit tests for FeatureExtractor, focusing on the fork-derivation post-processing step. Full
 * game-level tests are in FullMotifDetectorTest.
 */
public class FeatureExtractorTest {

  // --- FORK derivation tests ---

  @Test
  public void deriveFork_twoTargetsSameAttackerAndPly_createsForkOccurrences() {
    // Simulate two ATTACK occurrences at the same ply/attacker (a fork)
    List<GameFeatures.MotifOccurrence> attackOccs =
        List.of(
            GameFeatures.MotifOccurrence.attack(
                15, 8, "white", "Attack at move 8", "Ng6", "Ng6", "rh6", false, false),
            GameFeatures.MotifOccurrence.attack(
                15, 8, "white", "Attack at move 8", "Ng6", "Ng6", "ke8", false, false));

    Map<Motif, List<GameFeatures.MotifOccurrence>> allOccurrences = new EnumMap<>(Motif.class);
    allOccurrences.put(Motif.ATTACK, new ArrayList<>(attackOccs));
    Set<Motif> foundMotifs = EnumSet.of(Motif.ATTACK);

    FeatureExtractor.deriveForkFromAttack(allOccurrences, foundMotifs);

    assertThat(foundMotifs).contains(Motif.FORK);
    List<GameFeatures.MotifOccurrence> forkOccs = allOccurrences.get(Motif.FORK);
    assertThat(forkOccs).hasSize(2);

    // Both fork occurrences share the same attacker and ply
    assertThat(forkOccs).allMatch(o -> o.attacker().equals("Ng6"));
    assertThat(forkOccs).allMatch(o -> o.ply() == 15);
    assertThat(forkOccs).allMatch(o -> o.moveNumber() == 8);
    assertThat(forkOccs).allMatch(o -> o.side().equals("white"));
    assertThat(forkOccs).allMatch(o -> !o.isDiscovered());
    assertThat(forkOccs)
        .extracting(GameFeatures.MotifOccurrence::target)
        .containsExactlyInAnyOrder("rh6", "ke8");
  }

  @Test
  public void deriveFork_singleTargetPerAttacker_noFork() {
    List<GameFeatures.MotifOccurrence> attackOccs =
        List.of(
            GameFeatures.MotifOccurrence.attack(
                15, 8, "white", "Attack", "Ng6", "Ng6", "ke8", false, false));

    Map<Motif, List<GameFeatures.MotifOccurrence>> allOccurrences = new EnumMap<>(Motif.class);
    allOccurrences.put(Motif.ATTACK, new ArrayList<>(attackOccs));
    Set<Motif> foundMotifs = EnumSet.of(Motif.ATTACK);

    FeatureExtractor.deriveForkFromAttack(allOccurrences, foundMotifs);

    assertThat(foundMotifs).doesNotContain(Motif.FORK);
    assertThat(allOccurrences).doesNotContainKey(Motif.FORK);
  }

  @Test
  public void deriveFork_discoveredAttacksIgnored() {
    // Discovered attacks (isDiscovered=true) do not count toward fork grouping
    List<GameFeatures.MotifOccurrence> attackOccs =
        List.of(
            GameFeatures.MotifOccurrence.attack(
                15, 8, "white", "Attack", "Pf5", "Bg2", "rh6", true, false),
            GameFeatures.MotifOccurrence.attack(
                15, 8, "white", "Attack", "Pf5", "Bg2", "ke8", true, false));

    Map<Motif, List<GameFeatures.MotifOccurrence>> allOccurrences = new EnumMap<>(Motif.class);
    allOccurrences.put(Motif.ATTACK, new ArrayList<>(attackOccs));
    Set<Motif> foundMotifs = EnumSet.of(Motif.ATTACK);

    FeatureExtractor.deriveForkFromAttack(allOccurrences, foundMotifs);

    assertThat(foundMotifs).doesNotContain(Motif.FORK);
  }

  @Test
  public void deriveFork_differentPlies_noFork() {
    // Same attacker, different plies — not a fork
    List<GameFeatures.MotifOccurrence> attackOccs =
        List.of(
            GameFeatures.MotifOccurrence.attack(
                15, 8, "white", "Attack", "Ng6", "Ng6", "rh6", false, false),
            GameFeatures.MotifOccurrence.attack(
                17, 9, "white", "Attack", "Ng6", "Ng6", "ke8", false, false));

    Map<Motif, List<GameFeatures.MotifOccurrence>> allOccurrences = new EnumMap<>(Motif.class);
    allOccurrences.put(Motif.ATTACK, new ArrayList<>(attackOccs));
    Set<Motif> foundMotifs = EnumSet.of(Motif.ATTACK);

    FeatureExtractor.deriveForkFromAttack(allOccurrences, foundMotifs);

    assertThat(foundMotifs).doesNotContain(Motif.FORK);
  }

  @Test
  public void deriveFork_noAttackOccurrences_noFork() {
    Map<Motif, List<GameFeatures.MotifOccurrence>> allOccurrences = new EnumMap<>(Motif.class);
    Set<Motif> foundMotifs = EnumSet.noneOf(Motif.class);

    FeatureExtractor.deriveForkFromAttack(allOccurrences, foundMotifs);

    assertThat(foundMotifs).doesNotContain(Motif.FORK);
  }

  @Test
  public void deriveFork_blackFork_detectedCorrectly() {
    // Black queen forks on move 49
    List<GameFeatures.MotifOccurrence> attackOccs =
        List.of(
            GameFeatures.MotifOccurrence.attack(
                96, 49, "black", "Attack", "qg4", "qg4", "Ke6", false, false),
            GameFeatures.MotifOccurrence.attack(
                96, 49, "black", "Attack", "qg4", "qg4", "Rf5", false, false));

    Map<Motif, List<GameFeatures.MotifOccurrence>> allOccurrences = new EnumMap<>(Motif.class);
    allOccurrences.put(Motif.ATTACK, new ArrayList<>(attackOccs));
    Set<Motif> foundMotifs = EnumSet.of(Motif.ATTACK);

    FeatureExtractor.deriveForkFromAttack(allOccurrences, foundMotifs);

    assertThat(foundMotifs).contains(Motif.FORK);
    assertThat(allOccurrences.get(Motif.FORK))
        .allMatch(o -> o.side().equals("black"))
        .allMatch(o -> o.attacker().equals("qg4"));
  }

  @Test
  public void deriveFork_forkOccurrencesHaveCorrectFields() {
    List<GameFeatures.MotifOccurrence> attackOccs =
        List.of(
            GameFeatures.MotifOccurrence.attack(
                15, 8, "white", "Attack at move 8", "Ng6", "Ng6", "rh6", false, false),
            GameFeatures.MotifOccurrence.attack(
                15, 8, "white", "Attack at move 8", "Ng6", "Ng6", "ke8", false, false));

    Map<Motif, List<GameFeatures.MotifOccurrence>> allOccurrences = new EnumMap<>(Motif.class);
    allOccurrences.put(Motif.ATTACK, new ArrayList<>(attackOccs));
    Set<Motif> foundMotifs = EnumSet.of(Motif.ATTACK);

    FeatureExtractor.deriveForkFromAttack(allOccurrences, foundMotifs);

    List<GameFeatures.MotifOccurrence> forkOccs = allOccurrences.get(Motif.FORK);
    for (GameFeatures.MotifOccurrence fork : forkOccs) {
      assertThat(fork.ply()).isEqualTo(15);
      assertThat(fork.moveNumber()).isEqualTo(8);
      assertThat(fork.side()).isEqualTo("white");
      assertThat(fork.movedPiece()).isEqualTo("Ng6");
      assertThat(fork.attacker()).isEqualTo("Ng6");
      assertThat(fork.isDiscovered()).isFalse();
      assertThat(fork.isMate()).isFalse();
      assertThat(fork.pinType()).isNull();
    }
  }

  @Test
  public void featureExtractor_fullGame_forkDerivedFromAttack() {
    // Use a real game via PgnParser + GameReplayer + AttackDetector
    String pgn =
        """
        [Event "Test"]
        [White "W"]
        [Black "B"]
        [Result "1-0"]

        1. e4 e5 2. Nf3 Nc6 3. Bc4 Nf6 4. Ng5 d5 5. exd5 Nxd5 6. Nxf7 1-0
        """;

    List<MotifDetector> detectors = List.of(new AttackDetector());
    FeatureExtractor extractor =
        new FeatureExtractor(new PgnParser(), new GameReplayer(), detectors);
    GameFeatures features = extractor.extract(pgn);

    // Nxf7 attacks Qd8 and Rh8 — this is a fork
    assertThat(features.hasMotif(Motif.FORK)).isTrue();
    List<GameFeatures.MotifOccurrence> forkOccs = features.occurrences().get(Motif.FORK);
    assertThat(forkOccs).isNotEmpty();
    assertThat(forkOccs).allMatch(o -> o.moveNumber() == 6);
    assertThat(forkOccs).allMatch(o -> o.side().equals("white"));
    assertThat(forkOccs).allMatch(o -> o.attacker() != null && o.attacker().startsWith("N"));
  }
}
