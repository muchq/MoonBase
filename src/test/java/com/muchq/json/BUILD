load("//:tools/junit.bzl", "junit_tests")

junit_tests(
    name = "json_tests",
    size = "small",
    srcs = glob(["*.java"]),
    deps = [
        "//src/main/java/com/muchq/json",
        "//src/test/java/com/muchq/json/utils",
        "@third_party//com/fasterxml/jackson/core:jackson_core",
        "@third_party//com/fasterxml/jackson/core:jackson_databind",
        "@third_party//junit",
        "@third_party//org/assertj:assertj_core",
    ],
)
