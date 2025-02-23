load("@rules_cc//cc:defs.bzl", "cc_import")

alias(
    name = "raylib",
    actual = select({
        ":is_linux": ":raylib_linux",
        ":is_mac": ":raylib_macos",
    }),
    visibility = ["//visibility:public"],
)

config_setting(
    name = "is_mac",
    constraint_values = [
        "@platforms//cpu:aarch64",
        "@platforms//os:macos",
    ],
)

config_setting(
    name = "is_linux",
    constraint_values = [
        "@platforms//cpu:x86_64",
        "@platforms//os:linux",
    ],
)

cc_import(
    name = "raylib_linux",
    hdrs = [
        "include/raylib.h",
        "include/raymath.h",
        "include/rlgl.h",
    ],
    shared_library = "lib/libraylib.550.dylib",
    target_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
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
)
