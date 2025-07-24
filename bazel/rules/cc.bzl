"helpers for cc_binaries that need special linker options"

load("@rules_cc//cc:defs.bzl", "cc_binary")

def sdl_cc_binary(name = None, srcs = None, deps = None, visibility = None):
    cc_binary(
        name = name,
        srcs = srcs,
        linkopts = select({
            "@platforms//os:macos": [
                "-framework",
                "Metal",
                "-framework",
                "IOKit",
                "-framework",
                "CoreVideo",
                "-framework",
                "CoreAudio",
                "-framework",
                "CoreGraphics",
                "-framework",
                "CoreMedia",
                "-framework",
                "CoreHaptics",
                "-framework",
                "AppKit",
                "-framework",
                "Carbon",
                "-framework",
                "QuartzCore",
                "-framework",
                "AudioToolbox",
                "-framework",
                "GameController",
                "-framework",
                "ForceFeedback",
                "-framework",
                "AVFoundation",
                "-framework",
                "CoreFoundation",
                "-framework",
                "UniformTypeIdentifiers",
            ],
            "//conditions:default": [],
        }),
        visibility = visibility,
        deps = deps,
    )

