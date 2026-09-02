package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chessql.parser.Parser;
import org.junit.jupiter.api.Test;

public class QueryShapeTest {

  private static QueryShape shapeOf(String query) {
    return QueryShape.of(Parser.parse(query));
  }

  @Test
  public void collectsFieldsAndMotifsThroughEveryOperatorSortedAndDeduplicated() {
    QueryShape shape =
        shapeOf(
            "white.elo > 2500 AND (black.title = \"GM\" OR NOT eco IN [\"B90\", \"B91\"])"
                + " AND motif(fork) AND white.elo < 2800 AND sequence(pin THEN skewer)"
                + " ORDER BY motif_count(fork) DESC");

    assertThat(shape.fields()).containsExactly("black.title", "eco", "white.elo");
    assertThat(shape.motifs()).containsExactly("fork", "pin", "skewer");
    assertThat(shape.orderBy()).isEqualTo("fork");
  }

  @Test
  public void aQueryWithoutMotifsOrOrderingHasEmptyPartsRatherThanNulls() {
    QueryShape shape = shapeOf("num.moves >= 0");

    assertThat(shape.fields()).containsExactly("num.moves");
    assertThat(shape.motifs()).isEmpty();
    assertThat(shape.orderBy()).isEmpty();
  }

  /** The shape is the grammar's names only: a value never leaks into it. */
  @Test
  public void valuesDoNotAppearInTheShape() {
    QueryShape shape = shapeOf("white.username = \"hikaru\" AND opponent.elo > 2701");

    assertThat(shape.toString()).doesNotContain("hikaru").doesNotContain("2701");
  }
}
