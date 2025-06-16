load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

package(
    default_visibility = ["//visibility:public"],
)

filegroup(
    name = "all_srcs",
    srcs = glob(["**"]),
)

cmake(
    name = "raylib",
    lib_source = ":all_srcs",
    out_include_dir = "include",
    out_static_libs = ["libraylib.a"],
)
