"bzlmod extensions"

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _http_impl(ctx):  # buildifier: disable=unused-variable
    http_archive(
        name = "mongoose_cc",
        strip_prefix = "mongoose-7.18",
        sha256 = "8b661e8aceb00528fc21993afe218b1da0f0154575a61b63ce0791ad8b66b112",
        urls = ["https://github.com/cesanta/mongoose/archive/refs/tags/7.18.tar.gz"],
        build_file = "@//bazel/3p:mongoose.BUILD",
    )

mongoose = module_extension(implementation = _http_impl)
