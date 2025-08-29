"bzlmod extensions"

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _http_impl(ctx):  # buildifier: disable=unused-variable
    http_archive(
        name = "mongoose_cc",
        strip_prefix = "mongoose-7.19",
        sha256 = "7ae38c9d673559d1d9c1bd72c0bdd4a98a2cae995e87cae98b4604d5951762b8",
        urls = ["https://github.com/cesanta/mongoose/archive/refs/tags/7.19.tar.gz"],
        build_file = "@//bazel/3p:mongoose.BUILD",
    )

mongoose = module_extension(implementation = _http_impl)
