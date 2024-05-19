load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def register_http_archive_dependencies():
    http_archive(
        name = "bazel_features",
        sha256 = "0f23d75c7623d6dba1fd30513a94860447de87c8824570521fcc966eda3151c2",
        strip_prefix = "bazel_features-1.4.1",
        url = "https://github.com/bazel-contrib/bazel_features/releases/download/v1.4.1/bazel_features-v1.4.1.tar.gz",
    )

    http_archive(
        name = "com_github_bazelbuild_buildtools",
        sha256 = "05c3c3602d25aeda1e9dbc91d3b66e624c1f9fdadf273e5480b489e744ca7269",
        strip_prefix = "buildtools-6.4.0",
        urls = [
            "https://github.com/bazelbuild/buildtools/archive/refs/tags/v6.4.0.tar.gz",
        ],
    )

    http_archive(
        name = "rules_proto",
        sha256 = "71fdbed00a0709521ad212058c60d13997b922a5d01dbfd997f0d57d689e7b67",
        strip_prefix = "rules_proto-6.0.0-rc2",
        url = "https://github.com/bazelbuild/rules_proto/releases/download/6.0.0-rc2/rules_proto-6.0.0-rc2.tar.gz",
    )

    http_archive(
        name = "com_google_protobuf",
        sha256 = "dc167b7d23ec0d6e4a3d4eae1798de6c8d162e69fa136d39753aaeb7a6e1289d",
        strip_prefix = "protobuf-23.1",
        url = "https://github.com/protocolbuffers/protobuf/archive/refs/tags/v23.1.tar.gz",
    )

    http_archive(
        name = "com_github_grpc_grpc",
        patch_args = ["-p1"],
        patches = ["//bazel/patches:grpc_extra_deps.patch"],
        sha256 = "9cf1a69a921534ac0b760dcbefb900f3c2f735f56070bf0536506913bb5bfd74",
        strip_prefix = "grpc-1.55.0",
        urls = [
            "https://github.com/grpc/grpc/archive/refs/tags/v1.55.0.tar.gz",
        ],
    )

    http_archive(
        name = "com_google_absl",
        urls = ["https://github.com/abseil/abseil-cpp/archive/b1fb259ef793de57c2acefeeec07a6e3286ab9bc.zip"],
        sha256 = "6c7b209e73667e351e78f96599ce6c395e427be5589b511a648840b64b9c3467",
        strip_prefix = "abseil-cpp-b1fb259ef793de57c2acefeeec07a6e3286ab9bc",
    )

    http_archive(
        name = "com_google_googletest",
        sha256 = "0c3a1c0b47a21160ebb7610ae6407d2a6d291ebc5cddc0b99d091bf7641a815e",
        strip_prefix = "googletest-06f44bc951046150f1348598854b211afdcf37fc",
        urls = ["https://github.com/google/googletest/archive/06f44bc951046150f1348598854b211afdcf37fc.zip"],
    )

    http_archive(
        name = "rules_proto_grpc",
        sha256 = "928e4205f701b7798ce32f3d2171c1918b363e9a600390a25c876f075f1efc0a",
        strip_prefix = "rules_proto_grpc-4.4.0",
        urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/releases/download/4.4.0/rules_proto_grpc-4.4.0.tar.gz"],
    )

    http_archive(
        name = "mongoose_cc",
        strip_prefix = "mongoose-b379816178abdcd59135aa32f990a4b3bbbfb54b",
        patch_args = ["-p1"],
        patches = ["//bazel/patches:mongoose.patch"],
        sha256 = "a3ae70035a010b29cdd13c9bcae655d2b56bfdb724f43132cdbc99d7457f0b1b",
        urls = ["https://github.com/cesanta/mongoose/archive/b379816178abdcd59135aa32f990a4b3bbbfb54b.tar.gz"],
    )

    http_archive(
        name = "io_bazel_rules_go",
        sha256 = "f74c98d6df55217a36859c74b460e774abc0410a47cc100d822be34d5f990f16",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/rules_go/releases/download/v0.47.1/rules_go-v0.47.1.zip",
            "https://github.com/bazelbuild/rules_go/releases/download/v0.47.1/rules_go-v0.47.1.zip",
        ],
    )

    http_archive(
        name = "rules_jvm_external",
        strip_prefix = "rules_jvm_external-5.3",
        sha256 = "d31e369b854322ca5098ea12c69d7175ded971435e55c18dd9dd5f29cc5249ac",
        url = "https://github.com/bazelbuild/rules_jvm_external/releases/download/5.3/rules_jvm_external-5.3.tar.gz",
    )

    http_archive(
        name = "contrib_rules_jvm",
        sha256 = "2b710518847279f655a18a51a1629b033e4406f29609e73eb07ecfb6f0138d25",
        strip_prefix = "rules_jvm-0.13.0",
        url = "https://github.com/bazel-contrib/rules_jvm/releases/download/v0.13.0/rules_jvm-v0.13.0.tar.gz",
    )

    http_archive(
        name = "bazel_skylib",
        sha256 = "66ffd9315665bfaafc96b52278f57c7e2dd09f5ede279ea6d39b2be471e7e3aa",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.4.2/bazel-skylib-1.4.2.tar.gz",
            "https://github.com/bazelbuild/bazel-skylib/releases/download/1.4.2/bazel-skylib-1.4.2.tar.gz",
        ],
    )

    http_archive(
        name = "io_bazel_rules_scala",
        sha256 = "3b00fa0b243b04565abb17d3839a5f4fa6cc2cac571f6db9f83c1982ba1e19e5",
        strip_prefix = "rules_scala-6.5.0",
        url = "https://github.com/bazelbuild/rules_scala/releases/download/v6.5.0/rules_scala-v6.5.0.tar.gz",
    )

    http_archive(
        name = "hedron_compile_commands",
        sha256 = "3cd0e49f0f4a6d406c1d74b53b7616f5e24f5fd319eafc1bf8eee6e14124d115",
        strip_prefix = "bazel-compile-commands-extractor-3dddf205a1f5cde20faf2444c1757abe0564ff4c",
        url = "https://github.com/hedronvision/bazel-compile-commands-extractor/archive/3dddf205a1f5cde20faf2444c1757abe0564ff4c.tar.gz",
    )
