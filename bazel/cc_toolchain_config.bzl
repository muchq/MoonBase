"cc build helpers"

load("@bazel_tools//tools/cpp:cc_toolchain_config_lib.bzl", "tool_path")


def _impl(ctx):
    tool_paths = [
        tool_path(
            name = "gcc",
            path = "/usr/local/bin/gcc-13",
        ),
        tool_path(
            name = "ld",
            path = "/usr/bin/ld",
        ),
        tool_path(
            name = "ar",
            #path = "/usr/bin/ar",
            path = "/usr/local/bin/gcc-ar-13",
        ),
        tool_path(
            name = "cpp",
            path = "/usr/local/bin/g++-13",
        ),
        tool_path(
            name = "gcov",
            path = "/bin/false",
        ),
        tool_path(
            name = "nm",
            path = "/bin/false",
        ),
        tool_path(
            name = "objdump",
            path = "/bin/false",
        ),
        tool_path(
            name = "strip",
            path = "/bin/false",
        ),
    ]

    return cc_common.create_cc_toolchain_config_info(
        ctx = ctx,
        cxx_builtin_include_directories = [ # NEW
          "/usr/local/Cellar/gcc/13.1.0/include/",
          "/usr/local/Cellar/gcc/13.1.0/lib/gcc/current/gcc/x86_64-apple-darwin22/13/include/",
          "/usr/local/Cellar/gcc/13.1.0/lib/gcc/current/gcc/x86_64-apple-darwin22/13/include-fixed/",
          "/Library/Developer/CommandLineTools/SDKs/MacOSX13.sdk/usr/include/",
          "/usr/include",
        ],
        toolchain_identifier = "local",
        host_system_name = "local",
        target_system_name = "local",
        target_cpu = "darwin",
        target_libc = "unknown",
        compiler = "g++-13",
        abi_version = "unknown",
        abi_libc_version = "unknown",
        tool_paths = tool_paths, # NEW
    )

cc_toolchain_config_darwin = rule(
    implementation = _impl,
    attrs = {},
    provides = [CcToolchainConfigInfo],
)
