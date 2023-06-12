load("@io_bazel_rules_scala//scala:scala.bzl", "scala_library")

scala_library(
    name = "spitha",
    srcs = [
        "SparkApp.scala",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@third_party//org/apache/spark:spark_core_2_13",
        "@third_party//org/apache/spark:spark_sql_2_13",
        "@third_party//org/apache/spark:spark_tags_2_13",
        "@third_party//org/slf4j:slf4j_api",
    ],
)
