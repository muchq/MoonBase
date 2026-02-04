"""NullAway-enabled Java macros.

These macros wrap the standard java_library and java_binary rules to enable
NullAway static analysis by default. NullAway helps eliminate NullPointerExceptions
by performing null-safety analysis at compile time.

Usage:
    load("//bazel/rules:nullaway.bzl", "java_library", "java_binary")

    java_library(
        name = "my_lib",
        srcs = ["MyClass.java"],
        deps = ["@maven//:org_jspecify_jspecify"],
    )

To disable NullAway for a specific target, set nullaway = False:
    java_library(
        name = "my_lib",
        srcs = ["MyClass.java"],
        nullaway = False,
    )
"""

load("@rules_java//java:defs.bzl", _java_binary = "java_binary", _java_library = "java_library", _java_test = "java_test")

_NULLAWAY_PLUGIN = "//bazel/rules:nullaway"

_NULLAWAY_JAVACOPTS = [
    "-Xep:NullAway:ERROR",
    "-XepOpt:NullAway:OnlyNullMarked=true",
    "-XepOpt:NullAway:HandleTestAssertionLibraries=true",
]

def java_library(
        name,
        srcs = None,
        deps = None,
        plugins = None,
        javacopts = None,
        nullaway = True,
        **kwargs):
    """java_library with NullAway enabled by default.

    Args:
        name: Target name.
        srcs: Source files.
        deps: Dependencies.
        plugins: Additional annotation processor plugins.
        javacopts: Additional Java compiler options.
        nullaway: Enable NullAway checking (default: True).
        **kwargs: Additional arguments passed to java_library.
    """
    plugins = list(plugins or [])
    javacopts = list(javacopts or [])

    if nullaway:
        if _NULLAWAY_PLUGIN not in plugins:
            plugins.append(_NULLAWAY_PLUGIN)
        for opt in _NULLAWAY_JAVACOPTS:
            if opt not in javacopts:
                javacopts.append(opt)

    _java_library(
        name = name,
        srcs = srcs,
        deps = deps,
        plugins = plugins if plugins else None,
        javacopts = javacopts if javacopts else None,
        **kwargs
    )

def java_binary(
        name,
        srcs = None,
        deps = None,
        plugins = None,
        javacopts = None,
        nullaway = True,
        **kwargs):
    """java_binary with NullAway enabled by default.

    Args:
        name: Target name.
        srcs: Source files.
        deps: Dependencies.
        plugins: Additional annotation processor plugins.
        javacopts: Additional Java compiler options.
        nullaway: Enable NullAway checking (default: True).
        **kwargs: Additional arguments passed to java_binary.
    """
    plugins = list(plugins or [])
    javacopts = list(javacopts or [])

    if nullaway:
        if _NULLAWAY_PLUGIN not in plugins:
            plugins.append(_NULLAWAY_PLUGIN)
        for opt in _NULLAWAY_JAVACOPTS:
            if opt not in javacopts:
                javacopts.append(opt)

    _java_binary(
        name = name,
        srcs = srcs,
        deps = deps,
        plugins = plugins if plugins else None,
        javacopts = javacopts if javacopts else None,
        **kwargs
    )

def java_test(
        name,
        srcs = None,
        deps = None,
        plugins = None,
        javacopts = None,
        nullaway = True,
        **kwargs):
    """java_test with NullAway enabled by default.

    Args:
        name: Target name.
        srcs: Source files.
        deps: Dependencies.
        plugins: Additional annotation processor plugins.
        javacopts: Additional Java compiler options.
        nullaway: Enable NullAway checking (default: True).
        **kwargs: Additional arguments passed to java_test.
    """
    plugins = list(plugins or [])
    javacopts = list(javacopts or [])

    if nullaway:
        if _NULLAWAY_PLUGIN not in plugins:
            plugins.append(_NULLAWAY_PLUGIN)
        for opt in _NULLAWAY_JAVACOPTS:
            if opt not in javacopts:
                javacopts.append(opt)

    _java_test(
        name = name,
        srcs = srcs,
        deps = deps,
        plugins = plugins if plugins else None,
        javacopts = javacopts if javacopts else None,
        **kwargs
    )
