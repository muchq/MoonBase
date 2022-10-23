# cribbed and fixed https://github.com/bazelbuild/bazel/issues/1017#issuecomment-199326970
#
# skylark rule to generate a Junit4 TestSuite
# Assumes srcs are all .java Test files
# Assumes junit4 is already added to deps by the user.

_OUTPUT = """
import org.junit.runners.Suite;
import org.junit.runner.RunWith;

@RunWith(Suite.class)
@Suite.SuiteClasses({%s})
public class %s {}
"""

def _SafeIndex(l, val):
    for i, v in enumerate(l):
        if val == v:
            return i
    return -1

def _AsClassName(fname):
    fname = [x.path for x in fname.files.to_list()][0]
    toks = fname[:-5].split("/")
    findex = _SafeIndex(toks, "com")
    if findex == -1:
        fail("%s does not contain com", fname)
    return ".".join(toks[findex:]) + ".class"

def _impl(ctx):
    classes = ",".join(
        [_AsClassName(x) for x in ctx.attr.srcs]
    )
    ctx.actions.write(output = ctx.outputs.out, content = _OUTPUT % (
            classes, ctx.attr.outname))

_GenSuite = rule(
    implementation=_impl,
    attrs={
        "srcs": attr.label_list(allow_files=True),
        "outname": attr.string(),
    },
    outputs={"out": "%{name}.java"},
)

def junit_tests(name, srcs, **kwargs):
    suite_name = name + "TestSuite"
    _GenSuite(name=suite_name, srcs=srcs, outname=suite_name)
    native.java_test(name=name, test_class=suite_name, srcs = srcs + [":" + suite_name], **kwargs)
