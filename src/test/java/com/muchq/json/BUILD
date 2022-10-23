load("//:tools/junit.bzl", "junit_tests")

junit_tests(
    name = "json_tests",
    srcs = glob(["*.java"]),
    deps = [
      "//src/main/java/com/muchq/json",
      "//src/test/java/com/muchq/json/utils",
      "@maven//:com_fasterxml_jackson_core_jackson_core",
      "@maven//:com_fasterxml_jackson_core_jackson_databind",
      "@maven//:junit_junit",
      "@maven//:org_assertj_assertj_core",
    ],
)
