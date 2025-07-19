"bzlmod extensions"

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _http_impl(ctx):  # buildifier: disable=unused-variable
    http_archive(
        name = "sfml",
        urls = ["https://github.com/SFML/SFML/archive/refs/tags/3.0.1.zip"],
        integrity = "sha256-jHkTaMSvvLaJDWZ0FY+Pbp7ztOlvvUzugvbWH1p+iaU=",
        strip_prefix = "SFML-3.0.1",
        build_file = "@//bazel/3p:sfml.BUILD",
    )

sfml = module_extension(implementation = _http_impl)
