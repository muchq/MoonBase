load("@io_bazel_rules_scala//scala:scala.bzl", "scala_binary")

scala_binary(
    name = "Tracer",
    srcs = ["Tracer.scala"],
    main_class = "com.muchq.scraphics.Tracer",
    deps = [
        "@maven//:org_scala_lang_scala3_library_3",
        "@maven//:org_slf4j_slf4j_api",
    ],
)
