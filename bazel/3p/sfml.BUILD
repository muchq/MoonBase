load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

package(
    default_visibility = ["//visibility:public"],
)

filegroup(
    name = "all_srcs",
    srcs = glob(["**"]),
)

cmake(
    name = "sfml",
    defines = select({
        "@bazel_tools//src/conditions:darwin": [
            "CMAKE_OSX_DEPLOYMENT_TARGET=10.15",
        ],
        "//conditions:default": [],
    }),
    generate_args = select({
        "@bazel_tools//src/conditions:darwin": [
            "-DBUILD_SHARED_LIBS=false",
            "-DSFML_USE_SYSTEM_DEPS=false",
            "-DSFML_BUILD_AUDIO=true",
            "-DSFML_BUILD_GRAPHICS=true",
            # "-DSFML_BUILD_NETWORK=true",
            "-DSFML_BUILD_WINDOW=true",
            "-DCMAKE_BUILD_TYPE=Release",
        ],
        "//conditions:default": [
            "-DBUILD_SHARED_LIBS=false",
            "-DSFML_USE_SYSTEM_DEPS=true",
            "-DSFML_BUILD_AUDIO=true",
            "-DSFML_BUILD_GRAPHICS=true",
            # "-DSFML_BUILD_NETWORK=true",
            "-DSFML_BUILD_WINDOW=true",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
        ],
    }),
    lib_source = ":all_srcs",
    out_include_dir = "include",
    out_static_libs = [
        "libsfml-system-s.a",
        "libsfml-window-s.a",
        "libsfml-graphics-s.a",
        "libsfml-audio-s.a",
        # "libsfml-network-s.a",
    ],
    tags = ["requires-network"],
)
