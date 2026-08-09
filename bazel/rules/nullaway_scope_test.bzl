"""Guards that NullAway stays on by default, and that its exemptions only shrink.

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

These assert against `JavaInfo.compilation_info` and the compile action of a
library built by the macro under test, rather than against the text of java.bzl.
The difference is the point. Searching the file for option text passes when the
option sits in a comment, in a constant nothing reads, or in a macro that has
stopped propagating it — all of which leave NullAway silently off. A static
analysis fails by going quiet, so what a rule was actually configured with is
the only thing worth asserting.

Two things are deliberately not asserted here:

  - That any particular package is unchecked. Under this model that is what the
    opt-out guard covers, and a test naming individual packages would go red the
    day one was fixed.
  - That test sources are analyzed. They are not, anywhere, because
    java_test_suite is a passthrough to contrib_rules_jvm that adds neither the
    plugin nor these javacopts. That gap survives this change and is documented
    in CLAUDE.md; asserting it would pin a limitation in place.

Also not claimed: that an opt-out or an annotated root points at source that
exists. A package misspelled into one nobody has is well-formed and matches
nothing, and catching that needs a repo-wide source scan Bazel cannot express
here without making every Java file a dependency of this test. A stale opt-out
fails safe — it exempts nothing — but it also lingers unnoticed.
"""

load("@bazel_skylib//lib:unittest.bzl", "analysistest", "asserts")
load("@rules_java//java/common:java_info.bzl", "JavaInfo")
load(":java.bzl", "java_library")

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
    "com.muchq.games.one_d4.engine",
    "com.muchq.games.one_d4.motifs",
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

    annotated_root_test(
        name = name + "_annotated_root",
        target_under_test = name + "_fixture",
    )

    legacy_opt_outs_test(
        name = name + "_legacy_opt_outs",
        target_under_test = name + "_fixture",
    )

    plugin_test(
        name = name + "_plugin",
        target_under_test = name + "_fixture",
    )

    native.test_suite(
        name = name,
        tests = [
            name + "_severity",
            name + "_annotated_root",
            name + "_legacy_opt_outs",
            name + "_plugin",
        ],
    )
