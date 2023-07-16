load("@com_github_bazelbuild_buildtools//buildifier:def.bzl", "buildifier")
load("@io_bazel_rules_scala//scala:scala_toolchain.bzl", "scala_toolchain")
load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

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

buildifier(
    name = "buildifier",
)

cmake(
    name = "mongocxx",
    cache_entries = {
        "CMAKE_C_FLAGS": "-fPIC",
    },
    lib_source = "@mongocxx//:all_srcs",
    out_static_libs = ["libpcre.a"],
)

alias(
    name = "go-images",
    actual = "//go/images",
    visibility = ["//visibility:public"],
)
