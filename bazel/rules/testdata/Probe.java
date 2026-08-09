package com.muchq.platform.probe;

/**
 * Fixture for {@code //bazel/rules:rules_test}. Its contents do not matter — the guards read how
 * the {@code java_library} macro configured this target, and never compile it.
 *
 * <p>Named so that no guard can match it by accident. An earlier name contained "NullAway", which
 * silently satisfied the plugin guard's search for a NullAway jar among the compile inputs: the
 * fixture's own source file was the match, and the guard passed with the plugin removed.
 */
final class Probe {
  private Probe() {}
}
