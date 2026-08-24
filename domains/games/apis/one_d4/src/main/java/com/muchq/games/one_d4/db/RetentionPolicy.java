package com.muchq.games.one_d4.db;

import com.fasterxml.jackson.databind.JsonNode;
import com.muchq.platform.json.JsonUtils;
import java.io.IOException;
import java.io.InputStream;
import java.io.UncheckedIOException;
import java.time.Duration;

/**
 * How long indexed data survives, and how long a claim on it is good for.
 *
 * <p>Read from {@code retention_policy.json} on the classpath at class load (#1424). The C++ worker
 * reads the same file out of its own image at startup, so there is one set of numbers and neither
 * language holds a copy of them.
 *
 * <p>The reasoning — why requests outlive their games, why the lease is a fifth of the staleness
 * window, what the run ceiling is for, and why the three settle arms run in the order they do —
 * lives in {@code domains/games/apis/one_d4/README.md} under "Data Retention Policy".
 *
 * <p>A missing, unreadable or nonsensical file fails here, during class initialisation. Nothing
 * else in the object graph touches this class — every reader is a method body — so {@code
 * IndexerModule.retentionWindows()} is a {@code @Context} bean whose only job is to make that
 * happen at startup rather than at the first request that needed a window. That is the intended
 * outcome and the reason it is safe: a service that cannot read its retention windows should not
 * serve expiry dates it is guessing at, and it certainly should not answer {@code /health} 200
 * while doing it.
 *
 * <p>The file ships inside the deploy jar, so "missing" is a build error rather than something a
 * deploy can produce.
 *
 * <p>Field-level validation matches the C++ loader's exactly — present, integral, positive — so a
 * file one reader accepts and the other rejects is not a state this can reach. The relationships
 * <em>between</em> the windows are checked by the C++ loader, which refuses to start on a
 * violation, and asserted by {@code RetentionPolicyTest}, which fails the build on one; they are
 * not re-checked here, so a contradictory file that somehow got past both would leave this service
 * serving expiry dates while the worker refused to sweep. {@code RetentionPolicyTest} also pins
 * every shipped value to the numbers API.md and the README publish in prose.
 */
public final class RetentionPolicy {

  private static final JsonNode POLICY = load();

  /** Games and indexed periods are deleted once they are older than this. */
  public static final Duration PERIOD = seconds("period_seconds");

  /** Request rows are deleted once they are older than this. Must exceed {@link #PERIOD}. */
  public static final Duration REQUEST = seconds("request_seconds");

  /**
   * How long a request may sit untouched, with no worker holding a live lease anywhere, before it
   * is retired and the user is told.
   */
  public static final Duration STALE_REQUEST = seconds("stale_request_seconds");

  /** How long a worker's claim on a request is good for without renewal. */
  public static final Duration LEASE = seconds("lease_seconds");

  /** How often the holder renews — a quarter of {@link #LEASE}, so three misses are survivable. */
  public static final Duration LEASE_RENEWAL = seconds("lease_renewal_seconds");

  /**
   * How long one claim may go on being renewed before the worker has to let go (#1282). Must exceed
   * {@link #STALE_REQUEST}.
   */
  public static final Duration MAX_RUN = seconds("max_run_seconds");

  /**
   * Statement timeout for the sweep's writes. The hourly sweep is the C++ worker's, but the submit
   * path still reclaims the one tuple it is about to claim, under the same bound. {@link
   * StatementTimeouts#RETENTION_SWEEP_SECONDS} is this value, so the two cannot part.
   */
  public static final Duration SWEEP_STATEMENT_TIMEOUT = seconds("sweep_statement_timeout_seconds");

  /**
   * How many times a request may be claimed before it stops being claimable. Part of the claim
   * protocol rather than of retention, and read from the same file for the same reason the lease
   * vocabulary is: this service and both of the C++ worker's queues decide when to give up, and two
   * of them holding different numbers would retry a poisoned request forever or abandon a healthy
   * one early.
   */
  public static final int MAX_ATTEMPTS = count("max_attempts");

  private static JsonNode load() {
    try (InputStream in =
        RetentionPolicy.class.getClassLoader().getResourceAsStream("retention_policy.json")) {
      if (in == null) {
        throw new IllegalStateException(
            "retention_policy.json is not on the classpath; one_d4 cannot know its retention"
                + " windows");
      }
      return JsonUtils.mapper().readTree(in);
    } catch (IOException e) {
      throw new UncheckedIOException("retention_policy.json is unreadable", e);
    }
  }

  private static Duration seconds(String key) {
    return seconds(POLICY, key);
  }

  private static int count(String key) {
    return count(POLICY, key);
  }

  /**
   * One window, on the same terms the C++ loader reads it: present, integral, positive. Both
   * readers get the same file, so a file one accepts and the other rejects is the worst outcome
   * available — the service would serve expiry dates for windows the worker refuses to start on.
   *
   * <p>{@code isIntegralNumber} rather than {@code canConvertToLong}, which is a range check: it
   * answers true for {@code 300.5} and {@code asLong()} then truncates to 300, turning a unit
   * mistake into a plausible-looking window. Package-private so the failure branches are reachable
   * from a test without putting a broken file on the classpath.
   */
  static Duration seconds(JsonNode policy, String key) {
    JsonNode value = policy.get(key);
    if (value == null || !value.isIntegralNumber()) {
      throw new IllegalStateException("retention_policy.json is missing an integer " + key);
    }
    long seconds = value.asLong();
    if (seconds <= 0) {
      throw new IllegalStateException(
          "retention_policy.json has a non-positive " + key + ": " + seconds);
    }
    return Duration.ofSeconds(seconds);
  }

  /**
   * A count rather than a window, on the same terms: present, integral, positive. Package-private
   * for the same reason {@link #seconds(JsonNode, String)} is.
   */
  static int count(JsonNode policy, String key) {
    JsonNode value = policy.get(key);
    if (value == null || !value.isIntegralNumber()) {
      throw new IllegalStateException("retention_policy.json is missing an integer " + key);
    }
    long count = value.asLong();
    if (count <= 0) {
      throw new IllegalStateException(
          "retention_policy.json has a non-positive " + key + ": " + count);
    }
    return Math.toIntExact(count);
  }

  private RetentionPolicy() {}
}
