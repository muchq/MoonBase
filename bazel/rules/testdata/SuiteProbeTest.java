package com.muchq.platform.probe;

/**
 * Fixture for {@code //bazel/rules:rules_test}. Its contents do not matter — the guards read how
 * the {@code java_test_suite} macro configured this target, and never compile it.
 *
 * <p>The {@code Test} suffix is load-bearing: contrib_rules_jvm splits a suite's srcs on it, and
 * only sources that match become a {@code java_test} of their own. This is the half of the split
 * that carries the actual test cases, and the half that went unanalyzed longest.
 */
final class SuiteProbeTest {
  private SuiteProbeTest() {}
}
