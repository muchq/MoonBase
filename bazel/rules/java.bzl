"""Java rules with NullAway enabled.

These macros wrap java_library, java_binary, java_test and java_test_suite to enable
NullAway static analysis. NullAway helps eliminate NullPointerExceptions by performing
null-safety analysis at compile time.

Nothing has to be enabled per package: analysis is on for everything under com.muchq,
and what is listed instead is the exemptions — see _NULLAWAY_LEGACY_OPT_OUTS below.
That covers test sources as well as main ones, which also means an exemption covers
both: a test shares its package with the code it tests.

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

_JUNIT_RUNTIME_DEPS = [
    _artifact("org.junit.jupiter:junit-jupiter-engine"),
    _artifact("org.junit.platform:junit-platform-launcher"),
    _artifact("org.junit.platform:junit-platform-reporting"),
]

_NULLAWAY_PLUGIN = "//bazel/rules:nullaway"

_MICRONAUT_PLUGINS = [
    "//bazel/rules:micronaut_type_element_visitor_processor",
    "//bazel/rules:micronaut_aggregating_type_element_visitor_processor",
    "//bazel/rules:micronaut_bean_definition_inject_processor",
    "//bazel/rules:micronaut_package_element_visitor_processor",
]

# NullAway is on for everything under com.muchq. These packages predate that and
# still carry violations, so they are exempt until someone fixes them — 97 at the
# time this list was written, all of them here.
#
# The list only shrinks. Fixing a package and deleting its line is the intended
# direction and needs no ceremony; adding a line means code that was analyzed no
# longer is, and //bazel/rules:rules_test fails until the new entry is declared
# there too. That is the friction, and it is deliberate.
#
# Prefixes, so a subpackage is covered by its parent's entry.
_NULLAWAY_LEGACY_OPT_OUTS = [
    "com.muchq.games.chessql.compiler",
    "com.muchq.games.chessql.parser",
    "com.muchq.games.mcpserver.tools",
    "com.muchq.games.one_d4.api",
    "com.muchq.games.one_d4.db",
    "com.muchq.games.one_d4.e2e",
    "com.muchq.games.one_d4.engine",
    "com.muchq.games.one_d4.motifs",
    "com.muchq.games.one_d4.service",
    "com.muchq.games.one_d4.worker",
]

_JAVACOPTS = [
    "-XDcompilePolicy=simple",
    "-Xep:NullAway:ERROR",
    # Annotated at the root: a new package is analyzed the day it exists, rather
    # than the day someone remembers to add it here.
    "-XepOpt:NullAway:AnnotatedPackages=com.muchq",
    "-XepOpt:NullAway:UnannotatedSubPackages=" + ",".join(_NULLAWAY_LEGACY_OPT_OUTS),
]

def _analysis(plugins, javacopts, micronaut):
    """NullAway — and Micronaut for the library and binary macros — appended to a target's config.

    Every macro in this file routes through here, because the previous
    arrangement was four hand-written copies of the same append loop and one of
    them (`java_test_suite`) simply never had it. A macro that forgets is then a
    macro that never called this, which is visible at its one call site rather
    than buried in a body that looks like the others.

    Micronaut is the one real difference between the macros, and the split is by
    macro rather than by whether the sources are main or test: `java_library` and
    `java_binary` compile the beans, and running four annotation processors over
    every test compile in the repo buys nothing to offset the cost. Test code
    that does need a bean definition generated gets it the same way any other
    library does — a `testonly` `java_library` beside the suite, as in
    yodel's `filter_test_app` and one_d4's `e2e_support`.

    Args:
        plugins: Plugins the caller asked for.
        javacopts: Compiler options the caller asked for.
        micronaut: Whether to add the Micronaut annotation processors.

    Returns:
        A (plugins, javacopts) tuple, each a new list, with nothing duplicated.
    """
    plugins = list(plugins or [])
    javacopts = list(javacopts or [])

    if _NULLAWAY_PLUGIN not in plugins:
        plugins.append(_NULLAWAY_PLUGIN)

    if micronaut:
        for p in _MICRONAUT_PLUGINS:
            if p not in plugins:
                plugins.append(p)

    for opt in _JAVACOPTS:
        if opt not in javacopts:
            javacopts.append(opt)

    return plugins, javacopts

def java_test_suite(runner = "junit5", runtime_deps = [], plugins = None, javacopts = None, **kwargs):
    """java_test_suite defaulting to JUnit 5 (Jupiter), with NullAway enabled.

    Automatically adds JUnit Jupiter engine and platform runtime deps.

    contrib_rules_jvm splits the srcs into one `java_test` per `*Test.java` and a
    shared `-test-lib` for the helpers, and forwards `plugins` and `javacopts` to
    both — so setting them here analyzes every test source, not just the suite's
    helper classes.

    Args:
        runner: Test runner to use. Defaults to "junit5".
        runtime_deps: Additional runtime dependencies.
        plugins: Additional annotation processor plugins.
        javacopts: Additional Java compiler options.
        **kwargs: Arguments passed to java_test_suite.
    """
    plugins, javacopts = _analysis(plugins, javacopts, micronaut = False)

    _java_test_suite(
        runner = runner,
        runtime_deps = runtime_deps + _JUNIT_RUNTIME_DEPS,
        plugins = plugins,
        javacopts = javacopts,
        **kwargs
    )

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

    # Only add processors if there are sources to compile
    if srcs:
        plugins, javacopts = _analysis(plugins, javacopts, micronaut = True)

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

    # Only add processors if there are sources to compile
    if srcs:
        plugins, javacopts = _analysis(plugins, javacopts, micronaut = True)

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

    # Only add processors if there are sources to compile
    if srcs:
        plugins, javacopts = _analysis(plugins, javacopts, micronaut = False)

    _java_test(
        name = name,
        srcs = srcs,
        deps = deps if deps else None,
        plugins = plugins if plugins else None,
        javacopts = javacopts if javacopts else None,
        **kwargs
    )
