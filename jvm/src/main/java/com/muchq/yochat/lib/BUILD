load("//bazel/rules:nullaway.bzl", "java_library")

java_library(
    name = "lib",
    srcs = glob(["*.java"]),
    visibility = ["//visibility:public"],
    deps = [
        "@maven//:io_netty_netty_buffer",
        "@maven//:io_netty_netty_codec_base",
        "@maven//:io_netty_netty_common",
        "@maven//:io_netty_netty_handler",
        "@maven//:io_netty_netty_transport",
        "@maven//:org_slf4j_slf4j_api",
    ],
)
