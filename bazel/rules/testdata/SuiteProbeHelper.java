package com.muchq.platform.probe;

/**
 * Fixture for {@code //bazel/rules:rules_test}. Its contents do not matter — the guards read how
 * the {@code java_test_suite} macro configured this target, and never compile it.
 *
 * <p>Deliberately not named {@code *Test}: a suite's non-test sources are compiled once into a
 * shared {@code -test-lib} rather than into each test target, so this is a second compile with its
 * own configuration. It is where fixtures and stubs live — the code most likely to hold a
 * deliberate null — and the first real violation this change surfaced was in one of them.
 */
final class SuiteProbeHelper {
  private SuiteProbeHelper() {}
}
