load("@io_bazel_rules_scala//scala:scala.bzl", "scala_test")

scala_test(
    name = "example",
    size = "small",
    srcs = ["SparkExampleUnitTest.scala"],
    visibility = ["//visibility:public"],
    deps = [
        "//jvm/src/main/scala/com/muchq/spitha",
        "//jvm/src/main/scala/com/muchq/spitha/example:spark_example",
    ],
)
