load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def register_http_archive_dependencies():
    http_archive(
        name = "bazel_features",
        sha256 = "2cd9e57d4c38675d321731d65c15258f3a66438ad531ae09cb8bb14217dc8572",
        strip_prefix = "bazel_features-1.11.0",
        url = "https://github.com/bazel-contrib/bazel_features/releases/download/v1.11.0/bazel_features-v1.11.0.tar.gz",
    )

    http_archive(
        name = "com_github_bazelbuild_buildtools",
        sha256 = "39c59cb5352892292cbe3174055aac187edcb5324c9b4e2d96cb6e40bd753877",
        strip_prefix = "buildtools-7.1.2",
        urls = [
            "https://github.com/bazelbuild/buildtools/archive/refs/tags/v7.1.2.tar.gz",
        ],
    )

    http_archive(
        name = "rules_proto",
        sha256 = "303e86e722a520f6f326a50b41cfc16b98fe6d1955ce46642a5b7a67c11c0f5d",
        strip_prefix = "rules_proto-6.0.0",
        url = "https://github.com/bazelbuild/rules_proto/releases/download/6.0.0/rules_proto-6.0.0.tar.gz",
    )

    http_archive(
        name = "com_google_protobuf",
        sha256 = "8ff511a64fc46ee792d3fe49a5a1bcad6f7dc50dfbba5a28b0e5b979c17f9871",
        strip_prefix = "protobuf-25.2",
        url = "https://github.com/protocolbuffers/protobuf/archive/refs/tags/v25.2.tar.gz",
    )

    http_archive(
        name = "rules_java",
        urls = [
            "https://github.com/bazelbuild/rules_java/releases/download/7.6.1/rules_java-7.6.1.tar.gz",
        ],
        sha256 = "f8ae9ed3887df02f40de9f4f7ac3873e6dd7a471f9cddf63952538b94b59aeb3",
    )

    http_archive(
        name = "rules_python",
        sha256 = "4912ced70dc1a2a8e4b86cec233b192ca053e82bc72d877b98e126156e8f228d",
        strip_prefix = "rules_python-0.32.2",
        url = "https://github.com/bazelbuild/rules_python/releases/download/0.32.2/rules_python-0.32.2.tar.gz",
    )

    http_archive(
        name = "com_github_grpc_grpc",
        patch_args = ["-p1"],
        patches = ["//bazel/patches:grpc_extra_deps.patch"],
        sha256 = "493d9905aa09124c2f44268b66205dd013f3925a7e82995f36745974e97af609",
        strip_prefix = "grpc-1.63.0",
        urls = [
            "https://github.com/grpc/grpc/archive/refs/tags/v1.63.0.tar.gz",
        ],
    )

    # use commit pinned in grpc 1.63.0
    http_archive(
        name = "com_google_absl",
        urls = ["https://github.com/abseil/abseil-cpp/archive/4a2c63365eff8823a5221db86ef490e828306f9d.zip"],
        sha256 = "f49929d22751bf70dd61922fb1fd05eb7aec5e7a7f870beece79a6e28f0a06c1",
        strip_prefix = "abseil-cpp-4a2c63365eff8823a5221db86ef490e828306f9d",
    )

    http_archive(
        name = "com_google_googletest",
        sha256 = "c49a85648bc47567b24b24acbd8c3a399c05da407c91ef629601753c2ed6076e",
        strip_prefix = "googletest-305e5a238b3c8d11266fbafd85520fb6b3184851",
        urls = ["https://github.com/google/googletest/archive/305e5a238b3c8d11266fbafd85520fb6b3184851.zip"],
    )

    http_archive(
        name = "rules_proto_grpc",
        sha256 = "2a0860a336ae836b54671cbbe0710eec17c64ef70c4c5a88ccfd47ea6e3739bd",
        strip_prefix = "rules_proto_grpc-4.6.0",
        urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/releases/download/4.6.0/rules_proto_grpc-4.6.0.tar.gz"],
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
        strip_prefix = "rules_jvm_external-6.1",
        sha256 = "08ea921df02ffe9924123b0686dc04fd0ff875710bfadb7ad42badb931b0fd50",
        url = "https://github.com/bazelbuild/rules_jvm_external/releases/download/6.1/rules_jvm_external-6.1.tar.gz",
    )

    http_archive(
        name = "contrib_rules_jvm",
        sha256 = "2b710518847279f655a18a51a1629b033e4406f29609e73eb07ecfb6f0138d25",
        strip_prefix = "rules_jvm-0.13.0",
        url = "https://github.com/bazel-contrib/rules_jvm/releases/download/v0.13.0/rules_jvm-v0.13.0.tar.gz",
    )

    http_archive(
        name = "bazel_skylib",
        sha256 = "9f38886a40548c6e96c106b752f242130ee11aaa068a56ba7e56f4511f33e4f2",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.6.1/bazel-skylib-1.6.1.tar.gz",
            "https://github.com/bazelbuild/bazel-skylib/releases/download/1.6.1/bazel-skylib-1.6.1.tar.gz",
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
        sha256 = "f01636585c3fb61c7c2dc74df511217cd5ad16427528ab33bc76bb34535f10a1",
        strip_prefix = "bazel-compile-commands-extractor-a14ad3a64e7bf398ab48105aaa0348e032ac87f8",
        url = "https://github.com/hedronvision/bazel-compile-commands-extractor/archive/a14ad3a64e7bf398ab48105aaa0348e032ac87f8.tar.gz",
    )
