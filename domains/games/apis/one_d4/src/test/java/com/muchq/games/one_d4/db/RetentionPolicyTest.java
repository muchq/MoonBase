package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.fasterxml.jackson.databind.JsonNode;
import com.muchq.platform.json.JsonUtils;
import java.time.Duration;
import org.junit.jupiter.api.Test;

/**
 * The published windows and the relationships between them.
 *
 * <p>The values are asserted as literals, and this is the only place in the service where they are
 * literals: {@code RetentionPolicy} loads {@code retention_policy.json} at class load, so a test
 * that read the same file to check them would agree with itself no matter what the file said. These
 * numbers are a contract with prose nothing else gates — API.md's "30 days" and its "23-day gap"
 * paragraph, the README retention table, the web app's panel note — so changing a window means
 * changing this test, and changing this test means finding the sentences that stated the old one.
 *
 * <p>The relationships are correctness constraints. The C++ loader rejects a policy that violates
 * one, since it has no other gate; here they are asserted rather than enforced in the static
 * initializer, where a violation would surface as an {@code ExceptionInInitializerError} during
 * Micronaut startup instead of as a failing test.
 */
public class RetentionPolicyTest {

  @Test
  public void theShippedWindowsAreTheOnesTheDocsPublish() {
    assertThat(RetentionPolicy.PERIOD)
        .as("README: games and periods, 7 days")
        .isEqualTo(Duration.ofDays(7));
    assertThat(RetentionPolicy.REQUEST)
        .as("API.md and README: requests, 30 days")
        .isEqualTo(Duration.ofDays(30));
    assertThat(RetentionPolicy.STALE_REQUEST)
        .as("README: untouched for 1 hour")
        .isEqualTo(Duration.ofHours(1));
    assertThat(RetentionPolicy.LEASE)
        .as("README: a claim is good for 5 minutes")
        .isEqualTo(Duration.ofMinutes(5));
    assertThat(RetentionPolicy.LEASE_RENEWAL)
        .as("a quarter of the lease")
        .isEqualTo(Duration.ofSeconds(75));
    assertThat(RetentionPolicy.MAX_RUN)
        .as("README: a 6 hour ceiling on renewal")
        .isEqualTo(Duration.ofHours(6));
  }

  /**
   * The sweep timeout lives with the other statement timeouts rather than in RetentionPolicy, but
   * it comes from the same file — the C++ sweep bounds its writes with it, and the submit path's
   * reclaim still does here. Asserted through {@code StatementTimeouts}, which is where the rest of
   * the service reads it, so the bound the queries actually get is the one under test.
   */
  @Test
  public void theSweepTimeoutIsTheOneTheStoresUse() {
    assertThat(StatementTimeouts.RETENTION_SWEEP_SECONDS).isEqualTo(120);
    assertThat(RetentionPolicy.SWEEP_STATEMENT_TIMEOUT).isEqualTo(Duration.ofSeconds(120));
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

  /**
   * Four renewals inside one lease, so three consecutive missed beats — a GC pause, a blip — do not
   * cost a worker the range it is still working on.
   */
  @Test
  public void aLeaseSurvivesThreeMissedRenewals() {
    assertThat(RetentionPolicy.LEASE_RENEWAL.multipliedBy(4))
        .isLessThanOrEqualTo(RetentionPolicy.LEASE);
  }

  private static JsonNode policy(String json) {
    try {
      return JsonUtils.mapper().readTree(json);
    } catch (Exception e) {
      throw new AssertionError(e);
    }
  }

  /**
   * The reader's own guard, exercised directly. The shipped file is valid, so nothing else here
   * reaches these branches — and the branches are the whole reason reading a file at startup is
   * safe rather than merely convenient.
   */
  @Test
  public void aWindowThatIsAbsentIsRejected() {
    assertThatThrownBy(() -> RetentionPolicy.seconds(policy("{}"), "lease_seconds"))
        .isInstanceOf(IllegalStateException.class)
        .hasMessageContaining("lease_seconds");
  }

  /**
   * Seconds-with-a-fraction is a unit mistake, and the naive Jackson guard ({@code
   * canConvertToLong}) is a range check that answers true for it and truncates — turning 300.5 into
   * a plausible 300. The C++ loader rejects it; both read the same file, so this one has to as
   * well.
   */
  @Test
  public void aWindowThatIsNotAnIntegerIsRejected() {
    for (String value : new String[] {"300.5", "\"300\"", "true", "null"}) {
      assertThatThrownBy(
              () ->
                  RetentionPolicy.seconds(
                      policy("{\"lease_seconds\": " + value + "}"), "lease_seconds"))
          .as("%s was accepted as a window", value)
          .isInstanceOf(IllegalStateException.class)
          .hasMessageContaining("lease_seconds");
    }
  }

  /** A zero window deletes everything older than now, which is everything. */
  @Test
  public void aNonPositiveWindowIsRejected() {
    for (String value : new String[] {"0", "-1"}) {
      assertThatThrownBy(
              () ->
                  RetentionPolicy.seconds(
                      policy("{\"period_seconds\": " + value + "}"), "period_seconds"))
          .as("%s was accepted as a window", value)
          .isInstanceOf(IllegalStateException.class)
          .hasMessageContaining("period_seconds");
    }
  }

  /** The accepting path, so the guards above are not passing by rejecting everything. */
  @Test
  public void anIntegerWindowIsRead() {
    assertThat(RetentionPolicy.seconds(policy("{\"lease_seconds\": 300}"), "lease_seconds"))
        .isEqualTo(Duration.ofMinutes(5));
  }
}
