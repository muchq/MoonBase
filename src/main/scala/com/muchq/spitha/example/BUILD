load("@io_bazel_rules_scala//scala:scala.bzl", "scala_binary", "scala_library")

scala_library(
    name = "spark_example",
    srcs = [
        "SparkExample.scala",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//src/main/scala/com/muchq/spitha",
        "@third_party//org/apache/spark:spark_core_2_13",
        "@third_party//org/apache/spark:spark_sql_2_13",
        "@third_party//org/apache/spark:spark_tags_2_13",
        "@third_party//org/slf4j:slf4j_api",
    ],
)

scala_binary(
    name = "spark_example_app",
    srcs = ["SparkExample.scala"],
    main_class = "com.muchq.spitha.example.SparkExample",
    visibility = ["//visibility:public"],
    deps = [
        ":spark_example",
        "//src/main/scala/com/muchq/spitha",
        "@third_party//org/apache/spark:spark_core_2_13",
        "@third_party//org/apache/spark:spark_sql_2_13",
        "@third_party//org/apache/spark:spark_tags_2_13",
        "@third_party//org/slf4j:slf4j_api",
    ],
)
