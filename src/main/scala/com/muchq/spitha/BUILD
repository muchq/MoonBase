load("@io_bazel_rules_scala//scala:scala.bzl", "scala_library")
load("@io_bazel_rules_scala//scala:scala_toolchain.bzl", "scala_toolchain")

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

scala_toolchain(
    name = "diagnostics_reporter_toolchain_impl",
    enable_diagnostics_report = True,
    visibility = ["//visibility:public"],
)

toolchain(
    name = "diagnostics_reporter_toolchain",
    toolchain = "diagnostics_reporter_toolchain_impl",
    toolchain_type = "@io_bazel_rules_scala//scala:toolchain_type",
    visibility = ["//visibility:public"],
)
