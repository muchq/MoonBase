load("@io_bazel_rules_scala//scala:scala.bzl", "scala_binary", "scala_library")

scala_library(
    name = "spark_example",
    srcs = [
        "SparkExample.scala",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//jvm/src/main/scala/com/muchq/spitha",
        "@maven//:org_apache_spark_spark_core_2_13",
        "@maven//:org_apache_spark_spark_sql_2_13",
        "@maven//:org_apache_spark_spark_tags_2_13",
        "@maven//:org_slf4j_slf4j_api",
    ],
)

scala_binary(
    name = "spark_example_app",
    srcs = ["SparkExample.scala"],
    main_class = "com.muchq.spitha.example.SparkExample",
    visibility = ["//visibility:public"],
    deps = [
        ":spark_example",
        "//jvm/src/main/scala/com/muchq/spitha",
        "@maven//:org_apache_spark_spark_core_2_13",
        "@maven//:org_apache_spark_spark_sql_2_13",
        "@maven//:org_apache_spark_spark_tags_2_13",
        "@maven//:org_slf4j_slf4j_api",
    ],
)
