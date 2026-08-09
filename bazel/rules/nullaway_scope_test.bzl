"""Guards that NullAway's configuration reaches a real compile.

The plugin is attached to every java_library and java_binary here, so the wiring
reads as though null-safety analysis is on everywhere. It is not: reporting is
limited to the package roots in AnnotatedPackages, and a source outside them
compiles with violations and no diagnostic. That scope is deliberate, but it is
one edit away from being far smaller than anyone intends — and a static analysis
fails by going quiet, so narrowing it leaves every build green. Nothing fails,
which is the whole problem.

These assert against `JavaInfo.compilation_info.javac_options` on a real target
built by the macro under test, rather than against the text of java.bzl. The
difference is the point. Searching the file for option text passes when the
option sits in a comment, in a constant nothing reads, or in a macro that has
stopped propagating it — all four of which leave NullAway silently off while the
test stays green. What a rule was actually configured with is the only thing
worth asserting.

Not covered, and deliberately not claimed: that an annotated root corresponds to
source that exists. A root misspelled into a package nobody has (`platfrom`)
matches nothing and reports nothing, and is well-formed enough to pass anything
short of a repo-wide source scan — which Bazel cannot express here without
making every Java file in the tree a dependency of this test. The required-roots
guard below catches that for the roots that are named; a newly added root is on
its author.

Also deliberately not asserted: that com.muchq.games is unchecked, and that no
test source is analyzed anywhere (java_test_suite is a passthrough that adds
neither the plugin nor these javacopts). Both are true today and both are
documented in CLAUDE.md, but a test on either would go red the day someone
widened coverage. Widening must stay free; narrowing is what fails here.
"""

load("@bazel_skylib//lib:unittest.bzl", "analysistest", "asserts")
load("@rules_java//java/common:java_info.bzl", "JavaInfo")
load(":java.bzl", "java_library")

_SEVERITY = "-Xep:NullAway:ERROR"

_ANNOTATED_PACKAGES_PREFIX = "-XepOpt:NullAway:AnnotatedPackages="

# Package roots whose null-safety coverage something else already depends on.
# Adding to this list is free; the guard is that it must not shrink. Dropping a
# root is subtraction by omission — nothing errors, the domain simply stops
# being analyzed.
_REQUIRED_ROOTS = ["com.muchq.chat", "com.muchq.platform"]

def _javac_options(env):
    """The individual javacopts the target under test was actually configured with.

    Bazel hands these back as a handful of groups, each a single string holding
    several space-separated flags, some of them shell-quoted. Flattening to one
    flag per entry is what makes an exact-match assertion possible — a substring
    search over the raw groups would also be satisfied by a longer flag that
    merely starts the same way.
    """
    compilation_info = analysistest.target_under_test(env)[JavaInfo].compilation_info
    groups = compilation_info.javac_options

    # A depset on Bazel 7+, a list before it.
    if type(groups) != "list":
        groups = groups.to_list()

    options = []
    for group in groups:
        for token in group.split(" "):
            flag = token.strip()
            if len(flag) > 1 and flag.startswith("'") and flag.endswith("'"):
                flag = flag[1:-1]
            if flag:
                options.append(flag)
    return options

def _annotated_roots(options):
    for option in options:
        if option.startswith(_ANNOTATED_PACKAGES_PREFIX):
            return [
                root.strip()
                for root in option[len(_ANNOTATED_PACKAGES_PREFIX):].split(",")
                if root.strip()
            ]
    return None

def _severity_impl(ctx):
    """A violation must fail the build, not decorate it."""
    env = analysistest.begin(ctx)
    options = _javac_options(env)

    asserts.true(
        env,
        _SEVERITY in options,
        ("this library compiles without {severity}, so NullAway either reports nothing or " +
         "reports it as a warning in a log nobody reads. A null-safety regression now lands " +
         "with CI green. Got: {options}").format(severity = _SEVERITY, options = options),
    )
    return analysistest.end(env)

severity_test = analysistest.make(_severity_impl)

def _required_roots_impl(ctx):
    """Coverage is opt-in per package root, so losing one is silent."""
    env = analysistest.begin(ctx)
    options = _javac_options(env)
    roots = _annotated_roots(options)

    if roots == None:
        asserts.true(
            env,
            False,
            ("this library compiles with no {prefix} option at all. NullAway reports nothing " +
             "anywhere without it, however the plugin is wired. Got: {options}").format(
                prefix = _ANNOTATED_PACKAGES_PREFIX,
                options = options,
            ),
        )
        return analysistest.end(env)

    for required in _REQUIRED_ROOTS:
        asserts.true(
            env,
            required in roots,
            ("{required} is no longer analyzed: every source under it now compiles unchecked, " +
             "with no diagnostic and no build failure to say so. Adding roots is fine — this " +
             "fails only when one is removed. Got: {roots}").format(
                required = required,
                roots = roots,
            ),
        )
    return analysistest.end(env)

required_roots_test = analysistest.make(_required_roots_impl)

def _plugin_impl(ctx):
    """The flags are inert without the jar that implements them.

    Verified to be a real hole rather than a theoretical one: with the plugin
    dropped from the macro, `-Xep:NullAway:ERROR` is still passed, the compile's
    processorpath carries only the Micronaut processors, every library in the
    repo still builds, and no null-safety analysis runs at all. The two guards
    above pass throughout. Nothing anywhere goes red.

    Matched on jars specifically, and never on sources. The first version of
    this searched every compile input for the name and was satisfied by the
    fixture's own source file, which was called NullAwayProbe.java — so it
    passed with the plugin removed, reporting the fixture to itself.
    """
    env = analysistest.begin(ctx)

    javac = None
    for action in analysistest.target_actions(env):
        if action.mnemonic == "Javac":
            javac = action

    if javac == None:
        asserts.true(env, False, "no Javac action on the fixture, so nothing here was checked")
        return analysistest.end(env)

    on_the_compile = [
        an_input.path
        for an_input in javac.inputs.to_list()
        if an_input.extension == "jar" and "nullaway" in an_input.basename.lower()
    ]

    asserts.true(
        env,
        on_the_compile != [],
        "the NullAway jar is not an input to this library's compile, so the -Xep flags above " +
        "are inert: they are still passed, every source still compiles, and no null-safety " +
        "analysis runs. That combination is green everywhere and checks nothing.",
    )
    return analysistest.end(env)

plugin_test = analysistest.make(_plugin_impl)

def nullaway_scope_test_suite(name):
    """Declares the fixture library and the guards that read its configuration.

    Args:
        name: Name of the resulting test_suite.
    """

    # Built by the macro under test, so it carries whatever java_library
    # actually propagates today. Analysis-only — analysistest never runs the
    # compile, so this costs nothing to keep.
    java_library(
        name = name + "_fixture",
        srcs = ["testdata/Probe.java"],
        tags = ["manual"],
    )

    severity_test(
        name = name + "_severity",
        target_under_test = name + "_fixture",
    )

    required_roots_test(
        name = name + "_required_roots",
        target_under_test = name + "_fixture",
    )

    plugin_test(
        name = name + "_plugin",
        target_under_test = name + "_fixture",
    )

    native.test_suite(
        name = name,
        tests = [name + "_severity", name + "_required_roots", name + "_plugin"],
    )
