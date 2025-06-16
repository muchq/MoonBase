# This is copied from https://github.com/tarsir/SDL3_bazel
load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "srcs",
    srcs = glob(["**"]),
    visibility = ["//visibility:public"],
)

cmake(
    name = "sdl3",
    defines = select({
        "@bazel_tools//src/conditions:darwin": [
            "CMAKE_OSX_DEPLOYMENT_TARGET=10.13",
        ],
        "//conditions:default": [],
    }),
    generate_args = [
        "-DSDL_STATIC=true",
        "-DSDL_SHARED=false",
    ] + select({
        "@platforms//cpu:wasm32": ["-DEMSCRIPTEN=true"],
        "@platforms//os:wasi": ["-DEMSCRIPTEN=true"],
        "//conditions:default": [],
    }),
    lib_source = ":srcs",
    out_include_dir = "include",
    out_static_libs = select({
        "@bazel_tools//src/conditions:linux": ["libSDL3.a"],
        "@bazel_tools//src/conditions:darwin": ["libSDL3.a"],
        "@platforms//cpu:wasm32": ["libSDL3.a"],
        "@platforms//os:wasi": ["libSDL3.a"],
    }),
    visibility = ["//visibility:public"],
)