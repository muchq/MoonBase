package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import java.time.Duration;
import org.junit.jupiter.api.Test;

/**
 * The published windows and the relationships between them. The literals are a contract with prose
 * nothing else gates — API.md's "30 days" and its "23-day gap" paragraph, the README retention
 * table, the web app's panel note — and the relationships are correctness constraints asserted here
 * rather than in a static initializer, where a violation would surface as an {@code
 * ExceptionInInitializerError} during Micronaut startup instead of as a failing test.
 */
public class RetentionPolicyTest {

  @Test
  public void retentionWindowIsSevenDays() {
    assertThat(RetentionPolicy.PERIOD).isEqualTo(Duration.ofDays(7));
  }

  @Test
  public void requestRetentionWindowIsThirtyDays() {
    assertThat(RetentionPolicy.REQUEST).isEqualTo(Duration.ofDays(30));
  }

  @Test
  public void strandedRequestCutoffIsOneHour() {
    assertThat(RetentionPolicy.STALE_REQUEST).isEqualTo(Duration.ofHours(1));
  }

  @Test
  public void leaseIsFiveMinutes() {
    assertThat(RetentionPolicy.LEASE).isEqualTo(Duration.ofMinutes(5));
  }

  @Test
  public void runCeilingIsSixHours() {
    assertThat(RetentionPolicy.MAX_RUN).isEqualTo(Duration.ofHours(6));
  }

  /**
   * Not a style preference — a correctness constraint. {@code game_features.request_id} is a
   * foreign key onto {@code indexing_requests(id)}, so a request must outlive the games it produced
   * or the sweep would be deleting rows its own children still reference.
   */
  @Test
  public void requestsAreRetainedLongerThanTheGamesTheyProduced() {
    assertThat(RetentionPolicy.REQUEST).isGreaterThan(RetentionPolicy.PERIOD);
  }

  /**
   * The two clocks answer different questions and must not converge. The hour is for work nobody
   * has claimed, where the only evidence is the row's age; the lease is for work someone has, where
   * the owner itself reports in. A lease grown to the staleness cutoff makes the distinction
   * decorative.
   */
  @Test
  public void aDeadOwnerIsReclaimedFarSoonerThanAnUnclaimedRow() {
    assertThat(RetentionPolicy.LEASE).isLessThan(RetentionPolicy.STALE_REQUEST);
  }

  /**
   * A ceiling below the staleness window would cut runs short of the very window the system uses to
   * decide whether anything is happening at all — a run still going is the clearest evidence there
   * is that something is.
   */
  @Test
  public void theRunCeilingSitsAboveTheStalenessWindow() {
    assertThat(RetentionPolicy.MAX_RUN).isGreaterThan(RetentionPolicy.STALE_REQUEST);
  }
}
