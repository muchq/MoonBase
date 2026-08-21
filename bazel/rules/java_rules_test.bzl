"""Guards on what the java.bzl macros actually configure a compile with.

Mostly that NullAway stays on by default and that its exemptions only shrink;
also that the Micronaut processors run for the library and binary macros and not
for the test ones.

NullAway is annotated at `com.muchq`, so a new package is analyzed the day it
exists rather than the day someone remembers to add it to a list. What is listed
instead is the exemptions: packages that predate the default and still carry
violations, named one by one in `_NULLAWAY_LEGACY_OPT_OUTS` in java.bzl.

The polarity is the whole design. Under an opt-in list, coverage is whatever
someone last remembered to add, and a new domain is silently unchecked forever
with nothing to say so. Under an opt-out list, coverage is everything, and each
gap has a name, a line, and an obvious direction of travel.

So these guards are asymmetric on purpose:

  - Deleting an opt-out — fixing a package — passes freely. That is the
    direction this list is supposed to move, and it should need no ceremony.
  - Adding one fails until it is declared here too. Code that was analyzed no
    longer is, and that should cost a deliberate edit in two places rather than
    a quiet line in a build file.

These assert against `JavaInfo.compilation_info` and the compile actions of
fixtures built by the macros under test, rather than against the text of
java.bzl. The difference is the point. Searching the file for option text passes
when the option sits in a comment, in a constant nothing reads, or in a macro
that has stopped propagating it — all of which leave NullAway silently off. A
static analysis fails by going quiet, so what a rule was actually configured
with is the only thing worth asserting.

And every guard runs against every fixture, not just the java_library one,
because java_test_suite spent its whole life as a passthrough that added neither
the plugin nor these javacopts — no test source anywhere was analyzed, and
nothing went red to say so (#1340). A guard reading only java_library would have
been green throughout. The suite fixture covers both compiles contrib_rules_jvm
derives from one srcs list: the per-`*Test.java` java_test, and the shared
`-test-lib` holding everything else.

One thing is deliberately not asserted here: that any particular package is
unchecked. Under this model that is what the opt-out guard covers, and a test
naming individual packages would go red the day one was fixed.

Also not claimed: that an opt-out or an annotated root points at source that
exists. A package misspelled into one nobody has is well-formed and matches
nothing, and catching that needs a repo-wide source scan Bazel cannot express
here without making every Java file a dependency of this test. A stale opt-out
fails safe — it exempts nothing — but it also lingers unnoticed.
"""

load("@bazel_skylib//lib:unittest.bzl", "analysistest", "asserts")
load("@rules_java//java/common:java_info.bzl", "JavaInfo")
load(":java.bzl", "java_library", "java_test_suite")

_SEVERITY = "-Xep:NullAway:ERROR"

_ANNOTATED_PACKAGES_PREFIX = "-XepOpt:NullAway:AnnotatedPackages="

_UNANNOTATED_PACKAGES_PREFIX = "-XepOpt:NullAway:UnannotatedSubPackages="

# The root that makes analysis default-on. Losing it does not shrink coverage to
# something smaller — it turns everything off at once, silently.
_ANNOTATED_ROOT = "com.muchq"

# The exemptions this test knows about, and the only ones permitted. Every entry
# is legacy: it was carrying violations when NullAway went default-on.
#
# Deleting a line here after deleting it from java.bzl is the intended lifecycle
# and passes at every step. Adding one is what this list exists to make visible.
_LEGACY_OPT_OUTS = [
    "com.muchq.games.chessql.compiler",
    "com.muchq.games.chessql.parser",
    "com.muchq.games.mcpserver.tools",
    "com.muchq.games.one_d4.api",
    "com.muchq.games.one_d4.db",
    "com.muchq.games.one_d4.e2e",
    "com.muchq.games.one_d4.service",
    "com.muchq.games.one_d4.worker",
]

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

def _packages(options, prefix):
    """The comma-separated package list carried by `prefix`, or None if absent."""
    for option in options:
        if option.startswith(prefix):
            return [
                package.strip()
                for package in option[len(prefix):].split(",")
                if package.strip()
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

def _annotated_root_impl(ctx):
    """Analysis is default-on, and stays that way, because the root is annotated."""
    env = analysistest.begin(ctx)
    options = _javac_options(env)
    roots = _packages(options, _ANNOTATED_PACKAGES_PREFIX)

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

    asserts.true(
        env,
        _ANNOTATED_ROOT in roots,
        ("{root} is no longer annotated, so nothing is analyzed by default — coverage is now " +
         "whatever happens to be listed rather than everything minus the exemptions. Narrowing " +
         "to a sublist of {root} is exactly the opt-in model this replaced. Got: {roots}").format(
            root = _ANNOTATED_ROOT,
            roots = roots,
        ),
    )
    return analysistest.end(env)

annotated_root_test = analysistest.make(_annotated_root_impl)

def _legacy_opt_outs_impl(ctx):
    """Exemptions may be removed freely and added only deliberately."""
    env = analysistest.begin(ctx)
    options = _javac_options(env)
    opt_outs = _packages(options, _UNANNOTATED_PACKAGES_PREFIX)

    if opt_outs == None:
        # No exemptions at all is a legitimate end state — it means someone
        # fixed the last of them — so this is not a failure.
        return analysistest.end(env)

    for opt_out in opt_outs:
        asserts.true(
            env,
            opt_out in _LEGACY_OPT_OUTS,
            ("{opt_out} is exempt from NullAway but is not in this test's legacy list, so it " +
             "is a new exemption rather than an old one. Code that was analyzed no longer is. " +
             "If that is genuinely intended, add it here as well and say why in the review; " +
             "removing exemptions never needs this.").format(opt_out = opt_out),
        )
    return analysistest.end(env)

legacy_opt_outs_test = analysistest.make(_legacy_opt_outs_impl)

def _javac_action(env):
    """The fixture's compile action, or None if the rule produced no Javac at all."""
    javac = None
    for action in analysistest.target_actions(env):
        if action.mnemonic == "Javac":
            javac = action
    return javac

def _processor_jars(javac, name):
    """Basenames of the jars on this compile whose name carries `name`.

    Matched on jars specifically, and never on sources — see `_plugin_impl` for
    what that costs when forgotten. Sound here only because the fixtures declare
    no deps: with an empty classpath, a jar on the compile came from a plugin.
    """
    return [
        an_input.basename
        for an_input in javac.inputs.to_list()
        if an_input.extension == "jar" and name in an_input.basename.lower()
    ]

def _plugin_impl(ctx):
    """The flags are inert without the jar that implements them.

    Verified to be a real hole rather than a theoretical one: with the plugin
    dropped from the macro, `-Xep:NullAway:ERROR` is still passed, the compile's
    processorpath carries only the Micronaut processors, every library in the
    repo still builds, and no null-safety analysis runs at all. The other guards
    pass throughout. Nothing anywhere goes red.

    Matched on jars specifically, and never on sources. The first version of
    this searched every compile input for the name and was satisfied by the
    fixture's own source file, which was called NullAwayProbe.java — so it
    passed with the plugin removed, reporting the fixture to itself.
    """
    env = analysistest.begin(ctx)

    javac = _javac_action(env)
    if javac == None:
        asserts.true(env, False, "no Javac action on the fixture, so nothing here was checked")
        return analysistest.end(env)

    asserts.true(
        env,
        _processor_jars(javac, "nullaway") != [],
        "the NullAway jar is not an input to this library's compile, so the -Xep flags above " +
        "are inert: they are still passed, every source still compiles, and no null-safety " +
        "analysis runs. That combination is green everywhere and checks nothing.",
    )
    return analysistest.end(env)

plugin_test = analysistest.make(_plugin_impl)

def _micronaut_impl(ctx):
    """Micronaut processors run for the library and binary macros, deliberately not the test ones.

    This is the one thing the four macros do differently, and the difference is
    a choice rather than the drift it used to be: `java_library` and
    `java_binary` compile the beans, while running four annotation processors
    over every test compile in the repo would buy nothing to pay for it.

    Pinned in both directions because an unpinned choice is one somebody
    reverses by tidying. Adding the processors to the test macros looks like
    making the set consistent and costs build time everywhere; dropping them
    from the library and binary macros leaves the beans ungenerated.
    """
    env = analysistest.begin(ctx)

    javac = _javac_action(env)
    if javac == None:
        asserts.true(env, False, "no Javac action on the fixture, so nothing here was checked")
        return analysistest.end(env)

    on_the_compile = _processor_jars(javac, "micronaut")

    if ctx.attr.expected:
        asserts.true(
            env,
            on_the_compile != [],
            "no Micronaut processor jar is an input to this compile, so bean definitions are " +
            "not generated for these sources. Micronaut then finds no beans at runtime, which " +
            "fails as a missing injection point far from here rather than as a compile error.",
        )
    else:
        asserts.equals(
            env,
            [],
            on_the_compile,
            "this is a test-macro compile and it is running the Micronaut processors. " +
            "These compiles do not need generated bean definitions, and every test target " +
            "in the repo would pay for them. Test code that does need a bean generated " +
            "belongs in a `testonly` java_library beside the suite, which runs the " +
            "processors because it is a library — not in the suite's own `plugins`. " +
            "On this compile: {}".format(on_the_compile),
        )
    return analysistest.end(env)

micronaut_test = analysistest.make(
    _micronaut_impl,
    attrs = {"expected": attr.bool(mandatory = True)},
)

_GUARDS = {
    "severity": severity_test,
    "annotated_root": annotated_root_test,
    "legacy_opt_outs": legacy_opt_outs_test,
    "plugin": plugin_test,
}

def java_rules_test_suite(name):
    """Declares the fixtures and runs every guard against each of them.

    Args:
        name: Name of the resulting test_suite.
    """

    # Built by the macros under test, so each carries whatever that macro
    # actually propagates today. Analysis-only — analysistest never runs the
    # compiles, so these cost nothing to keep.
    java_library(
        name = name + "_fixture",
        srcs = ["testdata/Probe.java"],
        tags = ["manual"],
    )

    java_test_suite(
        name = name + "_suite_fixture",
        srcs = [
            "testdata/SuiteProbeHelper.java",
            "testdata/SuiteProbeTest.java",
        ],
        # Explicitly empty rather than omitted: contrib_rules_jvm appends the
        # shared -test-lib to deps, and appending to the default None is a
        # loading-phase crash there.
        deps = [],
        tags = ["manual"],
    )

    # Each entry names a target to read and whether that compile should be
    # running the Micronaut processors — the only property that differs between
    # the macros.
    #
    # The two suite entries use the names contrib_rules_jvm derives, which are
    # the only way to reach those compiles: `-test-lib` for the non-test
    # sources, and one target per `*Test.java`, named after the source path with
    # the extension dropped.
    fixtures = {
        "main": (name + "_fixture", True),
        "test_suite_lib": (name + "_suite_fixture-test-lib", False),
        "test_suite_test": ("testdata/SuiteProbeTest", False),
    }

    tests = []
    for fixture_label, (fixture, micronaut) in fixtures.items():
        for guard_label, guard in _GUARDS.items():
            test_name = "{}_{}_{}".format(name, fixture_label, guard_label)
            guard(
                name = test_name,
                target_under_test = fixture,
            )
            tests.append(test_name)

        micronaut_name = "{}_{}_micronaut".format(name, fixture_label)
        micronaut_test(
            name = micronaut_name,
            target_under_test = fixture,
            expected = micronaut,
        )
        tests.append(micronaut_name)

    native.test_suite(
        name = name,
        tests = tests,
    )
