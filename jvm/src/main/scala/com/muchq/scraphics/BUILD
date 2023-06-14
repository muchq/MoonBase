load("@io_bazel_rules_scala//scala:scala.bzl", "scala_binary")

scala_binary(
    name = "ray_tracer",
    srcs = [
        "App.scala",
    ],
    main_class = "com.muchq.scraphics.App",
    visibility = ["//visibility:public"],
    deps = [
        "//jvm/src/main/scala/com/muchq/scraphics/tracer",
        "@third_party//org/slf4j:slf4j_api",
    ],
)
