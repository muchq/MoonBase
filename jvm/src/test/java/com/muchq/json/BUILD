load("@contrib_rules_jvm//java:defs.bzl", "java_test_suite")

java_test_suite(
    name = "json_tests",
    size = "small",
    srcs = glob(["*.java"]),
    deps = [
        "//jvm/src/main/java/com/muchq/json",
        "//jvm/src/test/java/com/muchq/json/utils",
        "@third_party//com/fasterxml/jackson/core:jackson_core",
        "@third_party//com/fasterxml/jackson/core:jackson_databind",
        "@third_party//junit",
        "@third_party//org/assertj:assertj_core",
    ],
)
