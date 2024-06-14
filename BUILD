load("@com_github_bazelbuild_buildtools//buildifier:def.bzl", "buildifier", "buildifier_test")
load("@io_bazel_rules_scala//scala:scala_toolchain.bzl", "scala_toolchain")

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
    exclude_patterns = ["./.bazelbsp/*"],
)

buildifier_test(
    name = "buildifier_test",
    size = "small",
    timeout = "short",
    exclude_patterns = ["./.bazelbsp/*"],
    lint_mode = "warn",
    mode = "diff",
    no_sandbox = True,
    workspace = "//:WORKSPACE",
)

alias(
    name = "go-images",
    actual = "//go/images",
    visibility = ["//visibility:public"],
)
