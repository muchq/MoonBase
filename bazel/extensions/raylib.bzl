"bzlmod extensions"

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _http_impl(ctx):  # buildifier: disable=unused-variable
    http_archive(
        name = "raylib",
        urls = ["https://github.com/raysan5/raylib/archive/refs/tags/5.5.zip"],
        integrity = "sha256-AOinyF96UiHlbujk4cZkK3c+rnPSk3mpHIrFFJ6AP0I=",
        strip_prefix = "raylib-5.5",
        build_file = "@//bazel/3p:raylib.BUILD",
    )

raylib = module_extension(implementation = _http_impl)
