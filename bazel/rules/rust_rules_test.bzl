"""Guards the linker-warning suppression in bazel/rust.MODULE.bazel.

Three things per triple, because the flag alone is not the property worth
keeping: the suppression reaches rustc, the linker_messages lint is not allowed
instead, and the toolchain flags it compensates for still arrive — so it goes
red rather than outliving its reason.

Exec-config links are out of reach here; analysistest only transitions the
target platform.
"""

load("@bazel_skylib//lib:unittest.bzl", "analysistest", "asserts")
load("@rules_rs//rs:rust_binary.bzl", "rust_binary")

# Both spellings appear on one command line: rules_rs renders the toolchain's
# link arguments as --codegen=link-arg=, extra_rustc_flags arrive as written.
_LINK_ARG_PREFIXES = [
    "--codegen=link-arg=",
    "-Clink-arg=",
]

_QUIET_UNUSED_LINK_ARGS = "-Wno-unused-command-line-argument"

# Either one earns the suppression.
_UNCONSUMED_LINK_ARGS = [
    "-rtlib=",
    "--unwindlib=",
]

_LINT_SILENCERS = ["-A", "--allow"]

def _rustc_argv(env):
    """The fixture's link action, or None if it has none.

    The last Rustc action, as the sibling Javac guard also takes: under
    pipelining the first emits metadata and carries no link arguments at all.
    """
    argv = None
    for action in analysistest.target_actions(env):
        if action.mnemonic == "Rustc":
            argv = action.argv
    return argv

def _link_args(argv):
    """The linker arguments on a rustc command line, in either spelling."""
    args = []
    for arg in argv:
        for prefix in _LINK_ARG_PREFIXES:
            if arg.startswith(prefix):
                args.append(arg[len(prefix):])
    return args

def _quiet_unused_link_args_impl(ctx):
    env = analysistest.begin(ctx)
    triple = ctx.attr.triple

    argv = _rustc_argv(env)
    if argv == None:
        asserts.true(env, False, "no Rustc action on the fixture, so nothing here was checked")
        return analysistest.end(env)

    link_args = _link_args(argv)

    asserts.true(
        env,
        _QUIET_UNUSED_LINK_ARGS in link_args,
        "rustc is not passed {} for {}, so every Rust link built for it warns. ".format(
            _QUIET_UNUSED_LINK_ARGS,
            triple,
        ) + "Add the triple to `extra_rustc_flags` in bazel/rust.MODULE.bazel.",
    )

    silencers = [flag for flag in argv for prefix in _LINT_SILENCERS if flag.startswith(prefix)]
    asserts.equals(
        env,
        [],
        silencers,
        "lint-allow flag on this link: {}. Allowing linker_messages buys the same ".format(silencers) +
        "quiet build and hides every other linker diagnostic with it.",
    )

    earned = [arg for arg in link_args for prefix in _UNCONSUMED_LINK_ARGS if arg.startswith(prefix)]
    asserts.true(
        env,
        earned != [],
        "no {} reaches rustc for {} any more, so the suppression hides clang's ".format(
            " or ".join(_UNCONSUMED_LINK_ARGS),
            triple,
        ) + "only diagnostic for a misapplied runtime flag and buys nothing. Drop both.",
    )
    return analysistest.end(env)

def _guard_for(platform):
    """A guard reading the fixture as `platform` builds it.

    The label is canonicalized because config_settings resolves against the
    repository that declared the rule class — bazel_skylib, which cannot see
    @rules_rs.
    """
    return analysistest.make(
        _quiet_unused_link_args_impl,
        attrs = {"triple": attr.string(mandatory = True)},
        config_settings = {
            "//command_line_option:platforms": [
                str(Label("@rules_rs//rs/platforms:" + platform)),
            ],
        },
    )

# Globals because a rule has to be bound to one.
aarch64_apple_darwin_test = _guard_for("aarch64-apple-darwin")

aarch64_unknown_linux_gnu_test = _guard_for("aarch64-unknown-linux-gnu")

x86_64_unknown_linux_gnu_test = _guard_for("x86_64-unknown-linux-gnu")

# TARGET_TRIPLES in bazel/rust.MODULE.bazel; a module file cannot load it.
_GUARDS = {
    "aarch64-apple-darwin": aarch64_apple_darwin_test,
    "aarch64-unknown-linux-gnu": aarch64_unknown_linux_gnu_test,
    "x86_64-unknown-linux-gnu": x86_64_unknown_linux_gnu_test,
}

def rust_rules_test_suite(name):
    """Declares the fixture and checks it under every target platform.

    Args:
        name: Name of the resulting test_suite.
    """
    rust_binary(
        name = name + "_fixture",
        srcs = ["testdata/probe.rs"],
        crate_name = "probe",
        tags = ["manual"],
    )

    tests = []
    for triple, test in _GUARDS.items():
        test_name = "{}_{}".format(name, triple.replace("-", "_"))
        test(
            name = test_name,
            size = "small",
            target_under_test = name + "_fixture",
            triple = triple,
        )
        tests.append(test_name)

    native.test_suite(
        name = name,
        tests = tests,
    )
