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
        sha256 = "da288bf1daa6c04d03a9051781caa52aceb9163586bff9aa6cfb12f69b9395aa",
        strip_prefix = "protobuf-27.0",
        url = "https://github.com/protocolbuffers/protobuf/archive/refs/tags/v27.0.tar.gz",
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
        sha256 = "c5ad277fc21d4899f0e23f6f0337d8a2190d3c66c57ca868378be7c7bfa59fec",
        strip_prefix = "grpc-1.64.1",
        urls = [
            "https://github.com/grpc/grpc/archive/refs/tags/v1.64.1.tar.gz",
        ],
    )

    # use commit pinned in grpc 1.63.0
    http_archive(
        name = "com_google_absl",
        urls = ["https://github.com/abseil/abseil-cpp/archive/d06b82773e2306a99a8971934fb5845d5c04a170.zip"],
        sha256 = "491033c7cfd319b48bfdd12873b0623473279fb2adf961ef0b2f38125c677b52",
        strip_prefix = "abseil-cpp-d06b82773e2306a99a8971934fb5845d5c04a170",
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
        sha256 = "a06555618ed249fdfd8b138505de9f012a98eae4672ef00aa3cfc5f154ade6c7",
        strip_prefix = "rules_jvm-0.26.0",
        url = "https://github.com/bazel-contrib/rules_jvm/releases/download/v0.26.0/rules_jvm-v0.26.0.tar.gz",
    )

    http_archive(
        name = "bazel_skylib",
        sha256 = "d00f1389ee20b60018e92644e0948e16e350a7707219e7a390fb0a99b6ec9262",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.7.0/bazel-skylib-1.7.0.tar.gz",
            "https://github.com/bazelbuild/bazel-skylib/releases/download/1.7.0/bazel-skylib-1.7.0.tar.gz",
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
