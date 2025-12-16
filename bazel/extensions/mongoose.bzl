"bzlmod extensions"

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _http_impl(ctx):  # buildifier: disable=unused-variable
    http_archive(
        name = "mongoose_cc",
        strip_prefix = "mongoose-7.20",
        sha256 = "ede2371ff4e41e95fd7b2de83a7df727388307a64d3706046ead320defe35e7e",
        urls = ["https://github.com/cesanta/mongoose/archive/refs/tags/7.20.tar.gz"],
        build_file = "@//bazel/3p:mongoose.BUILD",
    )

mongoose = module_extension(implementation = _http_impl)
