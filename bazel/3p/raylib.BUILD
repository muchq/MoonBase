load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(
    default_visibility = ["//visibility:public"],
)

# raylib's own modules on GLFW from the BCR, as its CMake build does with
# USE_EXTERNAL_GLFW=ON (so no src/rglfw.c). OpenGL 3.3 goes through the
# bundled glad, loaded via glfwGetProcAddress, so nothing links libGL.
cc_library(
    name = "raylib",
    srcs = [
        "src/raudio.c",
        "src/rcore.c",
        "src/rmodels.c",
        "src/rshapes.c",
        "src/rtext.c",
        "src/rtextures.c",
        "src/utils.c",
    ],
    hdrs = [
        "src/raylib.h",
        "src/raymath.h",
        "src/rlgl.h",
    ],
    defines = [
        "PLATFORM_DESKTOP",
        "GRAPHICS_API_OPENGL_33",
    ],
    includes = ["src"],
    linkopts = select({
        "@platforms//os:macos": [
            "-framework",
            "IOKit",
            "-framework",
            "CoreGraphics",
            "-framework",
            "AppKit",
        ],
        "//conditions:default": [
            "-lm",
            "-lpthread",
            "-ldl",
        ],
    }),
    # rcore.c #includes its platform backend, and every module reaches into
    # src/external for stb, glad, miniaudio and friends.
    textual_hdrs = glob([
        "src/*.h",
        "src/platforms/*.c",
        "src/external/**/*.h",
        "src/external/**/*.c",
    ]),
    deps = ["@glfw"],
)
