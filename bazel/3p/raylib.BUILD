load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

package(
    default_visibility = ["//visibility:public"],
)

filegroup(
    name = "all_srcs",
    srcs = glob(["**"]),
)

cmake(
    name = "raylib_cmake",
    lib_source = ":all_srcs",
    out_include_dir = "include",
    out_static_libs = ["libraylib.a"],
)

# Main raylib target with platform-specific linking
cc_library(
    name = "raylib",
    deps = [":raylib_cmake"],
    linkopts = select({
        "@platforms//os:macos": [
            "-framework",
            "IOKit",
            "-framework",
            "CoreGraphics",
            "-framework",
            "AppKit",
        ],
        "//conditions:default": [],
    }),
    alwayslink = True,  # Ensures linking flags are always applied
)
