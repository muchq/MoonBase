"""Bazel build rules for Smithy code generation."""

def _smithy_codegen_impl(ctx):
    """Implementation for smithy code generation rules."""
    output_dir = ctx.actions.declare_directory(ctx.attr.name)

    args = ctx.actions.args()
    args.add(ctx.file.model.path)
    args.add(output_dir.path)

    if ctx.attr.package_name:
        args.add(ctx.attr.package_name)
    elif ctx.attr.module_name:
        args.add(ctx.attr.module_name)
    elif ctx.attr.crate_name:
        args.add(ctx.attr.crate_name)

    ctx.actions.run(
        inputs = [ctx.file.model],
        outputs = [output_dir],
        executable = ctx.executable._generator,
        arguments = [args],
        mnemonic = "SmithyCodegen",
        progress_message = "Generating %s code from Smithy model" % ctx.attr._language,
    )

    return [DefaultInfo(files = depset([output_dir]))]

# Java server generator rule
_smithy_java_server = rule(
    implementation = _smithy_codegen_impl,
    attrs = {
        "model": attr.label(
            mandatory = True,
            allow_single_file = [".json"],
            doc = "The Smithy model JSON file",
        ),
        "package_name": attr.string(
            doc = "The Java package name for generated code",
        ),
        "_generator": attr.label(
            default = "//smithy/generators/java:generate",
            executable = True,
            cfg = "exec",
        ),
        "_language": attr.string(default = "Java"),
    },
)

def smithy_java_server(name, model, package_name = None, **kwargs):
    """Generates Java server code from a Smithy model.

    Args:
        name: The name of the target
        model: The Smithy model JSON file
        package_name: The Java package name (optional, derived from namespace if not provided)
        **kwargs: Additional arguments to pass to the underlying rule
    """
    _smithy_java_server(
        name = name,
        model = model,
        package_name = package_name,
        **kwargs
    )

# Go server generator rule
_smithy_go_server = rule(
    implementation = _smithy_codegen_impl,
    attrs = {
        "model": attr.label(
            mandatory = True,
            allow_single_file = [".json"],
            doc = "The Smithy model JSON file",
        ),
        "module_name": attr.string(
            doc = "The Go module name for generated code",
        ),
        "_generator": attr.label(
            default = "//smithy/generators/go:generate",
            executable = True,
            cfg = "exec",
        ),
        "_language": attr.string(default = "Go"),
    },
)

def smithy_go_server(name, model, module_name = None, **kwargs):
    """Generates Go server code from a Smithy model.

    Args:
        name: The name of the target
        model: The Smithy model JSON file
        module_name: The Go module name (optional, derived from namespace if not provided)
        **kwargs: Additional arguments to pass to the underlying rule
    """
    _smithy_go_server(
        name = name,
        model = model,
        module_name = module_name,
        **kwargs
    )

# Rust server generator rule
_smithy_rust_server = rule(
    implementation = _smithy_codegen_impl,
    attrs = {
        "model": attr.label(
            mandatory = True,
            allow_single_file = [".json"],
            doc = "The Smithy model JSON file",
        ),
        "crate_name": attr.string(
            doc = "The Rust crate name for generated code",
        ),
        "_generator": attr.label(
            default = "//smithy/generators/rust:generate",
            executable = True,
            cfg = "exec",
        ),
        "_language": attr.string(default = "Rust"),
    },
)

def smithy_rust_server(name, model, crate_name = None, **kwargs):
    """Generates Rust server code from a Smithy model.

    Args:
        name: The name of the target
        model: The Smithy model JSON file
        crate_name: The Rust crate name (optional, derived from namespace if not provided)
        **kwargs: Additional arguments to pass to the underlying rule
    """
    _smithy_rust_server(
        name = name,
        model = model,
        crate_name = crate_name,
        **kwargs
    )

# C++ server generator rule
_smithy_cpp_server = rule(
    implementation = _smithy_codegen_impl,
    attrs = {
        "model": attr.label(
            mandatory = True,
            allow_single_file = [".json"],
            doc = "The Smithy model JSON file",
        ),
        "module_name": attr.string(
            doc = "The C++ module/namespace name for generated code",
        ),
        "_generator": attr.label(
            default = "//smithy/generators/cpp:generate",
            executable = True,
            cfg = "exec",
        ),
        "_language": attr.string(default = "C++"),
    },
)

def smithy_cpp_server(name, model, module_name = None, **kwargs):
    """Generates C++ server code from a Smithy model.

    Args:
        name: The name of the target
        model: The Smithy model JSON file
        module_name: The C++ module name (optional, derived from namespace if not provided)
        **kwargs: Additional arguments to pass to the underlying rule
    """
    _smithy_cpp_server(
        name = name,
        model = model,
        module_name = module_name,
        **kwargs
    )

# Combined generator macro for all languages
def smithy_server(name, model, languages = ["java", "go", "rust", "cpp"], **kwargs):
    """Generates server code for multiple languages from a Smithy model.

    Args:
        name: Base name for the targets
        model: The Smithy model JSON file
        languages: List of languages to generate (default: all)
        **kwargs: Additional arguments passed to individual language rules
    """
    if "java" in languages:
        smithy_java_server(
            name = name + "_java",
            model = model,
            package_name = kwargs.get("package_name"),
        )

    if "go" in languages:
        smithy_go_server(
            name = name + "_go",
            model = model,
            module_name = kwargs.get("module_name"),
        )

    if "rust" in languages:
        smithy_rust_server(
            name = name + "_rust",
            model = model,
            crate_name = kwargs.get("crate_name"),
        )

    if "cpp" in languages:
        smithy_cpp_server(
            name = name + "_cpp",
            model = model,
            module_name = kwargs.get("module_name"),
        )
