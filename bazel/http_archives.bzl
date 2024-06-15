load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def register_http_archive_dependencies():
    http_archive(
        name = "bazel_features",
        sha256 = "5d7e4eb0bb17aee392143cd667b67d9044c270a9345776a5e5a3cccbc44aa4b3",
        strip_prefix = "bazel_features-1.13.0",
        url = "https://github.com/bazel-contrib/bazel_features/releases/download/v1.13.0/bazel_features-v1.13.0.tar.gz",
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
        sha256 = "6fbe2e6f703bcd3a246529c2cab586ca12a98c4e641f5f71d51fde09eb48e9e7",
        strip_prefix = "protobuf-27.1",
        url = "https://github.com/protocolbuffers/protobuf/archive/refs/tags/v27.1.tar.gz",
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
        sha256 = "c682fc39baefc6e804d735e6b48141157b7213602cc66dbe0bf375b904d8b5f9",
        strip_prefix = "grpc-1.64.2",
        urls = [
            "https://github.com/grpc/grpc/archive/refs/tags/v1.64.2.tar.gz",
        ],
    )

    http_archive(
        name = "com_google_absl",
        urls = ["https://github.com/abseil/abseil-cpp/archive/f04e489056d9be93072bb633d9818b1e2add6316.zip"],
        sha256 = "503296d5ad0661260493393cd0f65104a5be711942b20adbda90798b1ea2871f",
        strip_prefix = "abseil-cpp-f04e489056d9be93072bb633d9818b1e2add6316",
    )

    http_archive(
        name = "com_google_googletest",
        sha256 = "ecb351335da20ab23ea5f14c107a10c475dfdd27d8a50d968757942280dffbe3",
        strip_prefix = "googletest-a7f443b80b105f940225332ed3c31f2790092f47",
        urls = ["https://github.com/google/googletest/archive/a7f443b80b105f940225332ed3c31f2790092f47.zip"],
    )

    http_archive(
        name = "rules_proto_grpc",
        sha256 = "2a0860a336ae836b54671cbbe0710eec17c64ef70c4c5a88ccfd47ea6e3739bd",
        strip_prefix = "rules_proto_grpc-4.6.0",
        urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/releases/download/4.6.0/rules_proto_grpc-4.6.0.tar.gz"],
    )

    http_archive(
        name = "mongoose_cc",
        strip_prefix = "mongoose-7.14",
        patch_args = ["-p1"],
        patches = ["//bazel/patches:mongoose.patch"],
        sha256 = "7c4aecf92f7f27f1cbb2cbda3c185c385f2b7af84f6bd7c0ce31b84742b15691",
        urls = ["https://github.com/cesanta/mongoose/archive/refs/tags/7.14.tar.gz"],
    )

    http_archive(
        name = "io_bazel_rules_go",
        sha256 = "33acc4ae0f70502db4b893c9fc1dd7a9bf998c23e7ff2c4517741d4049a976f8",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/rules_go/releases/download/v0.48.0/rules_go-v0.48.0.zip",
            "https://github.com/bazelbuild/rules_go/releases/download/v0.48.0/rules_go-v0.48.0.zip",
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
        sha256 = "e6cd8f54b7491fb3caea1e78c2c740b88c73c7a43150ec8a826ae347cc332fc7",
        strip_prefix = "rules_jvm-0.27.0",
        url = "https://github.com/bazel-contrib/rules_jvm/releases/download/v0.27.0/rules_jvm-v0.27.0.tar.gz",
    )

    http_archive(
        name = "bazel_skylib",
        sha256 = "bc283cdfcd526a52c3201279cda4bc298652efa898b10b4db0837dc51652756f",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.7.1/bazel-skylib-1.7.1.tar.gz",
            "https://github.com/bazelbuild/bazel-skylib/releases/download/1.7.1/bazel-skylib-1.7.1.tar.gz",
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
