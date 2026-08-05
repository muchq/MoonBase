package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.api.dto.QueryRequest;
import com.muchq.games.one_d4.api.dto.QueryResponse;
import java.time.Duration;
import java.time.Instant;
import java.util.List;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class FirstPageCacheTest {

  private MutableClock clock;
  private FirstPageCache cache;

  @BeforeEach
  public void setUp() {
    clock = new MutableClock(Instant.parse("2026-08-01T00:00:00Z"));
    cache = new FirstPageCache(clock);
  }

  private static QueryResponse response() {
    return new QueryResponse(List.of(), 0);
  }

  @Test
  public void matches_theExactFirstLoadRequest() {
    assertThat(cache.matches(FirstPageCache.defaultRequest())).isTrue();
    assertThat(cache.matches(new QueryRequest("num.moves >= 0", 25, 0, null))).isTrue();
  }

  @Test
  public void matches_toleratesSurroundingWhitespaceAndBlankPlayer() {
    assertThat(cache.matches(new QueryRequest("  num.moves >= 0  ", 25, 0, null))).isTrue();
    assertThat(cache.matches(new QueryRequest("num.moves >= 0", 25, 0, ""))).isTrue();
    assertThat(cache.matches(new QueryRequest("num.moves >= 0", 25, 0, "  "))).isTrue();
  }

  @Test
  public void matches_rejectsAnythingThatIsADifferentResultSet() {
    assertThat(cache.matches(new QueryRequest("white_elo >= 2000", 25, 0, null)))
        .as("different query")
        .isFalse();
    assertThat(cache.matches(new QueryRequest("num.moves >= 0", 50, 0, null)))
        .as("different page size")
        .isFalse();
    assertThat(cache.matches(new QueryRequest("num.moves >= 0", 25, 25, null)))
        .as("different page")
        .isFalse();
    assertThat(cache.matches(new QueryRequest("num.moves >= 0", 25, 0, "hikaru")))
        .as("player perspective changes motif filtering downstream")
        .isFalse();
    assertThat(cache.matches(new QueryRequest(null, 25, 0, null))).as("null query").isFalse();
  }

  @Test
  public void get_isEmptyBeforeAnythingIsStored() {
    assertThat(cache.get()).isEmpty();
  }

  @Test
  public void get_returnsWhatWasPut() {
    QueryResponse stored = response();
    cache.put(stored);
    assertThat(cache.get()).contains(stored);
  }

  @Test
  public void get_servesUntilMaxAgeAndNotAfter() {
    cache.put(response());

    clock.advance(FirstPageCache.MAX_AGE.minusSeconds(1));
    assertThat(cache.get()).as("just inside the window the snapshot is served").isPresent();

    // Caffeine's expireAfterWrite expires the entry once its age reaches the duration.
    clock.advance(Duration.ofSeconds(1));
    assertThat(cache.get()).as("at the boundary it is expired").isEmpty();
  }

  @Test
  public void put_resetsTheFreshnessWindow() {
    cache.put(response());
    clock.advance(FirstPageCache.MAX_AGE.plusSeconds(1));
    assertThat(cache.get()).isEmpty();

    QueryResponse fresh = response();
    cache.put(fresh);
    assertThat(cache.get()).contains(fresh);
  }

  @Test
  public void put_replacesTheEarlierSnapshot() {
    cache.put(response());
    QueryResponse newer = new QueryResponse(List.of(), 0);
    cache.put(newer);
    assertThat(cache.get()).containsSame(newer);
  }
}
