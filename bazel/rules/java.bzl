"""Java rules with NullAway enabled.

These macros wrap the standard java_library, java_binary, and java_test rules to enable
NullAway static analysis. NullAway helps eliminate NullPointerExceptions by performing
null-safety analysis at compile time.

To enable NullAway for a new package, add it

Usage:
    load("//bazel/rules:java.bzl", "java_library", "java_binary")

    java_library(
        name = "my_lib",
        srcs = ["MyClass.java"],
    )

"""

load("@contrib_rules_jvm//java:defs.bzl", _java_test_suite = "java_test_suite")
load("@rules_java//java:defs.bzl", _java_binary = "java_binary", _java_library = "java_library", _java_test = "java_test")
load("@rules_jvm_external//:defs.bzl", _artifact = "artifact")

artifact = _artifact
java_test_suite = _java_test_suite

_NULLAWAY_PLUGIN = "//bazel/rules:nullaway"

_MICRONAUT_PLUGINS = [
    "//bazel/rules:micronaut_type_element_visitor_processor",
    "//bazel/rules:micronaut_aggregating_type_element_visitor_processor",
    "//bazel/rules:micronaut_bean_definition_inject_processor",
    "//bazel/rules:micronaut_package_element_visitor_processor",
]

_JAVACOPTS = [
    "-XDcompilePolicy=simple",
    "-Xep:NullAway:ERROR",
    "-XepOpt:NullAway:AnnotatedPackages=com.muchq.platform,com.muchq.chat",
]

def java_library(
        name,
        srcs = None,
        deps = None,
        plugins = None,
        javacopts = None,
        **kwargs):
    """java_library with NullAway and Micronaut processors enabled by default.

    Args:
        name: Target name.
        srcs: Source files.
        deps: Dependencies.
        plugins: Additional annotation processor plugins.
        javacopts: Additional Java compiler options.
        **kwargs: Additional arguments passed to java_library.
    """
    plugins = list(plugins or [])
    javacopts = list(javacopts or [])
    deps = list(deps or [])

    # Only add processors and deps if there are sources to compile
    if srcs:
        if _NULLAWAY_PLUGIN not in plugins:
            plugins.append(_NULLAWAY_PLUGIN)

        for p in _MICRONAUT_PLUGINS:
            if p not in plugins:
                plugins.append(p)

        for opt in _JAVACOPTS:
            if opt not in javacopts:
                javacopts.append(opt)

    _java_library(
        name = name,
        srcs = srcs,
        deps = deps if deps else None,
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
        **kwargs):
    """java_binary with NullAway and Micronaut processors enabled by default.

    Args:
        name: Target name.
        srcs: Source files.
        deps: Dependencies.
        plugins: Additional annotation processor plugins.
        javacopts: Additional Java compiler options.
        **kwargs: Additional arguments passed to java_binary.
    """
    plugins = list(plugins or [])
    javacopts = list(javacopts or [])
    deps = list(deps or [])

    # Only add processors and deps if there are sources to compile
    if srcs:
        if _NULLAWAY_PLUGIN not in plugins:
            plugins.append(_NULLAWAY_PLUGIN)

        for p in _MICRONAUT_PLUGINS:
            if p not in plugins:
                plugins.append(p)

        for opt in _JAVACOPTS:
            if opt not in javacopts:
                javacopts.append(opt)

    _java_binary(
        name = name,
        srcs = srcs,
        deps = deps if deps else None,
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
        **kwargs):
    """java_test with NullAway support.

    Args:
        name: Target name.
        srcs: Source files.
        deps: Dependencies.
        plugins: Additional annotation processor plugins.
        javacopts: Additional Java compiler options.
        **kwargs: Additional arguments passed to java_test.
    """
    plugins = list(plugins or [])
    javacopts = list(javacopts or [])
    deps = list(deps or [])

    # Only add processors and deps if there are sources to compile
    if srcs:
        if _NULLAWAY_PLUGIN not in plugins:
            plugins.append(_NULLAWAY_PLUGIN)

        for opt in _JAVACOPTS:
            if opt not in javacopts:
                javacopts.append(opt)

    _java_test(
        name = name,
        srcs = srcs,
        deps = deps if deps else None,
        plugins = plugins if plugins else None,
        javacopts = javacopts if javacopts else None,
        **kwargs
    )
