"bzlmod extensions"

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _http_impl(ctx):  # buildifier: disable=unused-variable
    http_archive(
        name = "sdl3",
        build_file = "@//bazel/3p:sdl3.BUILD",
        sha256 = "6340e58879b2d15830c8460d2f589a385c444d1faa2a4828a9626c7322562be8",
        strip_prefix = "SDL3-3.2.16",
        url = "https://github.com/libsdl-org/SDL/releases/download/release-3.2.16/SDL3-3.2.16.tar.gz",
    )

sdl3 = module_extension(implementation = _http_impl)
