load("//bazel/rules:java.bzl", "artifact", "java_library", "java_test_suite")

java_library(
    name = "json",
    srcs = ["src/main/java/com/muchq/platform/json/JsonUtils.java"],
    visibility = ["//visibility:public"],
    deps = [
        artifact("com.fasterxml.jackson.core:jackson-core"),
        artifact("com.fasterxml.jackson.core:jackson-databind"),
        artifact("com.fasterxml.jackson.datatype:jackson-datatype-guava"),
        artifact("com.fasterxml.jackson.datatype:jackson-datatype-jdk8"),
        artifact("com.fasterxml.jackson.datatype:jackson-datatype-jsr310"),
    ],
)

java_test_suite(
    name = "json_tests",
    size = "small",
    srcs = [
        "src/test/java/com/muchq/platform/json/GuavaWidget.java",
        "src/test/java/com/muchq/platform/json/Java8Widget.java",
        "src/test/java/com/muchq/platform/json/JsonUtilsTest.java",
    ],
    deps = [
        ":json",
        artifact("com.fasterxml.jackson.core:jackson-annotations"),
        artifact("com.fasterxml.jackson.core:jackson-core"),
        artifact("com.fasterxml.jackson.core:jackson-databind"),
        artifact("junit:junit"),
        artifact("com.google.guava:guava"),
        artifact("org.assertj:assertj-core"),
    ],
)
