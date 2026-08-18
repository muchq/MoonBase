package com.muchq.games.one_d4.parity;

import static org.assertj.core.api.Assertions.assertThat;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.List;
import org.junit.jupiter.api.Test;

/**
 * Asserts the checked-in parity golden still matches what this pipeline extracts.
 *
 * <p>The golden is the oracle for the C++ port (#1389 phase 2), and an oracle nobody checks is a
 * file that quietly stops describing anything. This is the half that keeps it honest: change a
 * detector and this fails, so the golden is regenerated deliberately and the C++ side's diff shows
 * up in the same change.
 *
 * <p>Regenerate with:
 *
 * <pre>
 * bazel run //domains/games/apis/one_d4:motif_dump_main -- \
 *   $PWD/domains/games/apis/one_d4/src/test/resources/hikaru_corpus.pgn \
 *   &gt; domains/games/apis/one_d4/src/test/resources/motif_parity_golden.tsv
 * </pre>
 */
public class MotifGoldenTest {

  @Test
  public void goldenMatchesWhatThePipelineExtracts() throws IOException {
    List<String> games = MotifDump.split(read("/hikaru_corpus.pgn"));
    assertThat(games).hasSize(500);

    List<String> expected = List.of(read("/motif_parity_golden.tsv").split("\n"));
    List<String> actual = List.of(MotifDump.dump(games).split("\n"));

    // Row-wise rather than whole-string: a one-row change in 14k should say
    // which row, not "the files differ".
    assertThat(actual).as("occurrence rows").containsExactlyElementsOf(expected);
  }

  private static String read(String resource) throws IOException {
    try (InputStream in = MotifGoldenTest.class.getResourceAsStream(resource)) {
      assertThat(in).as("%s on the test classpath", resource).isNotNull();
      return new String(in.readAllBytes(), StandardCharsets.UTF_8);
    }
  }
}
