load("@io_bazel_rules_scala//scala:scala.bzl", "scala_library")

scala_library(
    name = "spitha",
    srcs = [
        "SparkApp.scala",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@maven//:org_apache_spark_spark_core_2_13",
        "@maven//:org_apache_spark_spark_sql_2_13",
        "@maven//:org_apache_spark_spark_tags_2_13",
        "@maven//:org_slf4j_slf4j_api",
    ],
)
