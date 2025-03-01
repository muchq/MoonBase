"bzlmod extensions"

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _http_impl(ctx):  # buildifier: disable=unused-variable
    http_archive(
        name = "mongoose_cc",
        strip_prefix = "mongoose-7.16",
        sha256 = "f2c42135f7bc34b3d10b6401e9326a20ba5dd42d4721b6a526826ba31c1679fd",
        urls = ["https://github.com/cesanta/mongoose/archive/refs/tags/7.16.tar.gz"],
        build_file = "@//bazel/3p:mongoose.BUILD",
    )

mongoose = module_extension(implementation = _http_impl)
