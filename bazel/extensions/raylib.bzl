"bzlmod extensions"

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _http_impl(module_ctx):
    host_os = module_ctx.os.name
    host_cpu = module_ctx.os.arch

    if host_os == "mac os x" and host_cpu == "aarch64":
        url = "https://github.com/raysan5/raylib/releases/download/5.5/raylib-5.5_macos.tar.gz"
        sha256 = "930c67b676963c6cffbd965814664523081ecbf3d30fc9df4211d0064aa6ba39"
        lib = "shared_library = \"lib/libraylib.550.dylib\""
        strip_prefix = "raylib-5.5_macos"
    elif host_os == "linux" and host_cpu == "amd64":
        url = "https://github.com/raysan5/raylib/releases/download/5.5/raylib-5.5_linux_amd64.tar.gz"
        sha256 = "3d95ef03d5b38dfa55c0a16ca122d382134b078f0e5b270b52fe7eae0549c000"
        lib = "static_library = \"lib/libraylib.a\""
        strip_prefix = "raylib-5.5_linux_amd64"
    else:
        fail("Unsupported host OS or CPU: {} {}".format(host_os, host_cpu))

    http_archive(
        name = "raylib",
        url = url,
        sha256 = sha256,
        strip_prefix = strip_prefix,
        build_file_content = """\
load("@rules_cc//cc:defs.bzl", "cc_import")

cc_import(
    name = "raylib",
    hdrs = [
        "include/raylib.h",
        "include/raymath.h",
        "include/rlgl.h",
    ],
    {},
    visibility = ["//visibility:public"],
)
        """.format(lib),
    )

raylib = module_extension(implementation = _http_impl)
