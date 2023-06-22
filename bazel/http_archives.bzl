load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def register_http_archive_dependencies():
    http_archive(
        name = "com_github_grpc_grpc",
        patch_args = ["-p1"],
        patches = ["//bazel/patches:grpc_extra_deps.patch"],
        sha256 = "d8c3180df613759e705aabde77798a463b8d2dad08f182cf4cbdc6d8c9d0ebdd",
        strip_prefix = "grpc-fd843629c89a22fc920fbbda8bcd79aa3b86add4",
        urls = [
            "https://github.com/grpc/grpc/archive/fd843629c89a22fc920fbbda8bcd79aa3b86add4.tar.gz",
        ],
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
        sha256 = "6b65cb7917b4d1709f9410ffe00ecf3e160edf674b78c54a894471320862184f",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/rules_go/releases/download/v0.39.0/rules_go-v0.39.0.zip",
            "https://github.com/bazelbuild/rules_go/releases/download/v0.39.0/rules_go-v0.39.0.zip",
        ],
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

    RULES_SCALA_VERSION = "12d60d203591d92572c812f345b45babff688230"

    http_archive(
        name = "io_bazel_rules_scala",
        sha256 = "5144514f81e63a3337e56d86b2924a22a1d5d9f273e482c2f2fb09639f6388fa",
        strip_prefix = "rules_scala-%s" % RULES_SCALA_VERSION,
        type = "zip",
        url = "https://github.com/bazelbuild/rules_scala/archive/%s.zip" % RULES_SCALA_VERSION,
    )

    http_archive(
        name = "rules_rust",
        sha256 = "50ec4b84a7ec5370f5882d52f4a1e6b8a75de2f8dcc0a4403747b69b2c4ef5b1",
        urls = ["https://github.com/bazelbuild/rules_rust/releases/download/0.23.0/rules_rust-v0.23.0.tar.gz"],
    )

    http_archive(
        name = "com_github_bazelbuild_buildtools",
        sha256 = "977a0bd4593c8d4c8f45e056d181c35e48aa01ad4f8090bdb84f78dca42f47dc",
        strip_prefix = "buildtools-6.1.2",
        urls = [
            "https://github.com/bazelbuild/buildtools/archive/refs/tags/v6.1.2.tar.gz",
        ],
    )
