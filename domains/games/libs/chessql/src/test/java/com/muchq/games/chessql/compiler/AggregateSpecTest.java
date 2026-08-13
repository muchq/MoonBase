package com.muchq.games.chessql.compiler;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatCode;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.chessql.compiler.AggregateSpec.Order;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

/**
 * The aggregate's shaping rules, pinned away from the SQL they produce.
 *
 * <p>{@link AggregateSpec} is where "a score ranking needs a player" is enforced, and it is
 * enforced by construction so both entry points inherit it — the REST validator builds a spec, and
 * so does the compiler's own {@code compileAggregate(pq, groupBy, player)} overload. The compiler
 * relies on that: the score ordering divides {@code wins} and {@code draws}, columns only a
 * player-scoped SELECT list carries, so a spec that allowed the combination would compile SQL the
 * database rejects.
 */
public class AggregateSpecTest {

  @Test
  public void anAbsentOrderingIsCount() {
    assertThat(Order.fromWire(null)).isEqualTo(Order.COUNT);
    assertThat(Order.fromWire("")).isEqualTo(Order.COUNT);
    assertThat(Order.fromWire("   ")).isEqualTo(Order.COUNT);
  }

  @Test
  public void orderingNamesAreCaseInsensitiveAndTrimmed() {
    assertThat(Order.fromWire("count")).isEqualTo(Order.COUNT);
    assertThat(Order.fromWire("COUNT")).isEqualTo(Order.COUNT);
    assertThat(Order.fromWire("score")).isEqualTo(Order.SCORE);
    assertThat(Order.fromWire(" Score ")).isEqualTo(Order.SCORE);
  }

  @Test
  public void anUnknownOrderingIsRejectedAndSaysWhatIsAccepted() {
    assertThatThrownBy(() -> Order.fromWire("elo"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("orderBy")
        .hasMessageContaining("\"count\"")
        .hasMessageContaining("\"score\"")
        .hasMessageContaining("elo");
    // Near-misses are not silently accepted either: this is an exact-name match, not a prefix.
    assertThatThrownBy(() -> Order.fromWire("scores")).isInstanceOf(IllegalArgumentException.class);
    assertThatThrownBy(() -> Order.fromWire("score desc"))
        .isInstanceOf(IllegalArgumentException.class);
  }

  @Test
  public void scoreOrderingWithoutAPlayerIsRejected() {
    assertThatThrownBy(() -> new AggregateSpec(List.of("opening_family"), null, Order.SCORE, 0))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("orderBy \"score\" requires a player");
    // A blank player is no player, so it must be refused for the same reason rather than
    // producing a spec whose SELECT list would carry no wins/draws columns to order by.
    assertThatThrownBy(() -> new AggregateSpec(List.of("opening_family"), "  ", Order.SCORE, 0))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("requires a player");
  }

  /** The control: the same ordering with a player is the request this feature exists to serve. */
  @Test
  public void scoreOrderingWithAPlayerIsAccepted() {
    assertThatCode(() -> new AggregateSpec(List.of("opening_family"), "hikaru", Order.SCORE, 10))
        .doesNotThrowAnyException();
  }

  @Test
  public void aBlankPlayerCarriesNoOutcomeMetrics() {
    assertThat(new AggregateSpec(List.of("eco"), null, Order.COUNT, 0).hasOutcomeMetrics())
        .isFalse();
    assertThat(new AggregateSpec(List.of("eco"), "   ", Order.COUNT, 0).hasOutcomeMetrics())
        .isFalse();
    assertThat(new AggregateSpec(List.of("eco"), "hikaru", Order.COUNT, 0).hasOutcomeMetrics())
        .isTrue();
  }

  @Test
  public void thePlayerIsStrippedSoTheCompiledPredicatesSeeOneSpelling() {
    assertThat(new AggregateSpec(List.of("eco"), "  hikaru  ", Order.COUNT, 0).player())
        .isEqualTo("hikaru");
  }

  @Test
  public void aNegativeFloorIsNoFloor() {
    assertThat(new AggregateSpec(List.of("eco"), null, Order.COUNT, -5).minGames()).isZero();
    assertThat(new AggregateSpec(List.of("eco"), null, Order.COUNT, 7).minGames()).isEqualTo(7);
  }

  @Test
  public void anAbsentOrderDefaultsRatherThanNullingOut() {
    assertThat(new AggregateSpec(List.of("eco"), null, null, 0).order()).isEqualTo(Order.COUNT);
  }

  /** A spec handed a mutable list must not change under the caller afterwards. */
  @Test
  public void theGroupByIsCopied() {
    List<String> mutable = new ArrayList<>(List.of("opening_family"));
    AggregateSpec spec = new AggregateSpec(mutable, "hikaru", Order.COUNT, 0);
    mutable.add("eco");

    assertThat(spec.groupBy()).containsExactly("opening_family");
  }

  @Test
  public void everyOrderingHasAWireNameAndTheRosterListsThemAll() {
    for (Order order : Order.values()) {
      assertThat(order.wireName()).isNotBlank();
      assertThat(Order.roster()).contains(order.wireName());
      assertThat(Order.fromWire(order.wireName())).isEqualTo(order);
    }
  }
}
