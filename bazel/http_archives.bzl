load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def register_http_archive_dependencies():
    http_archive(
        name = "com_github_grpc_grpc",
        patch_args = ["-p1"],
        patches = ["//bazel_patches:grpc_extra_deps.patch"],
        sha256 = "d8c3180df613759e705aabde77798a463b8d2dad08f182cf4cbdc6d8c9d0ebdd",
        strip_prefix = "grpc-fd843629c89a22fc920fbbda8bcd79aa3b86add4",
        urls = [
            "https://github.com/grpc/grpc/archive/fd843629c89a22fc920fbbda8bcd79aa3b86add4.tar.gz",
        ],
    )

    http_archive(
        name = "rules_proto_grpc",
        sha256 = "928e4205f701b7798ce32f3d2171c1918b363e9a600390a25c876f075f1efc0a",
        strip_prefix = "rules_proto_grpc-4.4.0",
        urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/releases/download/4.4.0/rules_proto_grpc-4.4.0.tar.gz"],
    )

    http_archive(
        name = "io_bazel_rules_go",
        sha256 = "6b65cb7917b4d1709f9410ffe00ecf3e160edf674b78c54a894471320862184f",
        urls = [
            "https://mirror.bazel_patches.build/github.com/bazelbuild/rules_go/releases/download/v0.39.0/rules_go-v0.39.0.zip",
            "https://github.com/bazelbuild/rules_go/releases/download/v0.39.0/rules_go-v0.39.0.zip",
        ],
    )

    http_archive(
        name = "contrib_rules_jvm",
        sha256 = "548f0583192ff79c317789b03b882a7be9b1325eb5d3da5d7fdcc4b7ca69d543",
        strip_prefix = "rules_jvm-0.9.0",
        url = "https://github.com/bazel_patches-contrib/rules_jvm/archive/refs/tags/v0.9.0.tar.gz",
    )

    SKYLIB_VERSION = "1.0.3"

    RULES_SCALA_VERSION = "887c9be387734d2a49adab441d7a68414e30cbee"

    http_archive(
        name = "bazel_skylib",
        sha256 = "1c531376ac7e5a180e0237938a2536de0c54d93f5c278634818e0efc952dd56c",
        type = "tar.gz",
        url = "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/{}/bazel-skylib-{}.tar.gz".format(SKYLIB_VERSION, SKYLIB_VERSION),
    )

    http_archive(
        name = "io_bazel_rules_scala",
        sha256 = "3dc6881307d5787bf6d06269e1fea25a87ab36c70775b9102576ce3fb6fcd260",
        strip_prefix = "rules_scala-%s" % RULES_SCALA_VERSION,
        type = "zip",
        url = "https://github.com/bazelbuild/rules_scala/archive/%s.zip" % RULES_SCALA_VERSION,
    )
