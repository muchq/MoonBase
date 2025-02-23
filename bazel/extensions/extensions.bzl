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

    http_archive(
        name = "raylib_macos",
        urls = ["https://github.com/raysan5/raylib/releases/download/5.5/raylib-5.5_macos.tar.gz"],
        sha256 = "930c67b676963c6cffbd965814664523081ecbf3d30fc9df4211d0064aa6ba39",
        strip_prefix = "raylib-5.5_macos",
        build_file = "@//bazel/3p:raylib.BUILD",
    )
    
    http_archive(
        name = "raylib_linux",
        urls = ["https://github.com/raysan5/raylib/releases/download/5.5/raylib-5.5_linux_amd64.tar.gz"],
        sha256 = "3d95ef03d5b38dfa55c0a16ca122d382134b078f0e5b270b52fe7eae0549c000",
        strip_prefix = "raylib-5.5_linux_amd64",
        build_file = "@//bazel/3p:raylib.BUILD",
    )

non_module_deps = module_extension(implementation = _http_impl)
