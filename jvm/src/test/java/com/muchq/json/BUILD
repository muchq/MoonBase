load("@contrib_rules_jvm//java:defs.bzl", "java_test_suite")

java_test_suite(
    name = "json_tests",
    size = "small",
    srcs = glob(["*.java"]),
    deps = [
        "//jvm/src/main/java/com/muchq/json",
        "//jvm/src/test/java/com/muchq/json/utils",
        "@maven//:com_fasterxml_jackson_core_jackson_core",
        "@maven//:com_fasterxml_jackson_core_jackson_databind",
        "@maven//:junit_junit",
        "@maven//:org_assertj_assertj_core",
    ],
)
