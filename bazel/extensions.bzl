"bzlmod extensions"

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
#load("@hedron_compile_commands//:workspace_setup.bzl", "hedron_compile_commands_setup")

#def _hedron_impl(ctx):  # buildifier: disable=unused-variable
#    hedron_compile_commands_setup()
#
#hedron_setup = module_extension(
#    implementation = _hedron_impl,
#)

def _http_impl(ctx):  # buildifier: disable=unused-variable
    http_archive(
        name = "mongoose_cc",
        strip_prefix = "mongoose-7.16",
        patch_args = ["-p1"],
        patches = ["//bazel/patches:mongoose.patch"],
        sha256 = "f2c42135f7bc34b3d10b6401e9326a20ba5dd42d4721b6a526826ba31c1679fd",
        urls = ["https://github.com/cesanta/mongoose/archive/refs/tags/7.16.tar.gz"],
    )

non_module_deps = module_extension(implementation = _http_impl)
