load("@rules_cc//cc:defs.bzl", "cc_import")

cc_import(
    name = "raylib_linux",
    hdrs = [
        "include/raylib.h",
        "include/raymath.h",
        "include/rlgl.h",
    ],
    shared_library = "lib/libraylib.so.550",
    target_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    visibility = ["//visibility:public"],
)

cc_import(
    name = "raylib_macos",
    hdrs = [
        "include/raylib.h",
        "include/raymath.h",
        "include/rlgl.h",
    ],
    shared_library = "lib/libraylib.550.dylib",
    target_compatible_with = [
        "@platforms//os:macos",
        "@platforms//cpu:aarch64",
    ],
    visibility = ["//visibility:public"],
)
