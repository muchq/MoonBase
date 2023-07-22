load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def register_http_archive_dependencies():
    http_archive(
        name = "com_github_bazelbuild_buildtools",
        sha256 = "977a0bd4593c8d4c8f45e056d181c35e48aa01ad4f8090bdb84f78dca42f47dc",
        strip_prefix = "buildtools-6.1.2",
        urls = [
            "https://github.com/bazelbuild/buildtools/archive/refs/tags/v6.1.2.tar.gz",
        ],
    )

    http_archive(
        name = "rules_proto",
        sha256 = "bc12122a5ae4b517fa423ea03a8d82ea6352d5127ea48cb54bc324e8ab78493c",
        strip_prefix = "rules_proto-af6481970a34554c6942d993e194a9aed7987780",
        urls = [
            "https://github.com/bazelbuild/rules_proto/archive/af6481970a34554c6942d993e194a9aed7987780.tar.gz",
        ],
    )

    http_archive(
        name = "com_github_grpc_grpc",
        patch_args = ["-p1"],
        patches = ["//bazel/patches:grpc_extra_deps.patch"],
        sha256 = "64c3756f8f4ac3a876655f6a04f4d9f6858c77612d79200d0528ad923d5550c7",
        strip_prefix = "grpc-8871dab19b4ab5389e28474d25cfeea61283265c",
        urls = [
            "https://github.com/grpc/grpc/archive/8871dab19b4ab5389e28474d25cfeea61283265c.tar.gz",
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
        sha256 = "6b65cb7917b4d1709f9410ffe00ecf3e160edf674b78c54a894471320862184f",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/rules_go/releases/download/v0.39.0/rules_go-v0.39.0.zip",
            "https://github.com/bazelbuild/rules_go/releases/download/v0.39.0/rules_go-v0.39.0.zip",
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

    RULES_SCALA_VERSION = "12d60d203591d92572c812f345b45babff688230"

    http_archive(
        name = "io_bazel_rules_scala",
        patch_args = ["-p1"],
        patches = ["//bazel/patches:rules_scala_33.patch"],
        sha256 = "5144514f81e63a3337e56d86b2924a22a1d5d9f273e482c2f2fb09639f6388fa",
        strip_prefix = "rules_scala-%s" % RULES_SCALA_VERSION,
        type = "zip",
        url = "https://github.com/bazelbuild/rules_scala/archive/%s.zip" % RULES_SCALA_VERSION,
    )

    http_archive(
        name = "hedron_compile_commands",
        sha256 = "3cd0e49f0f4a6d406c1d74b53b7616f5e24f5fd319eafc1bf8eee6e14124d115",
        strip_prefix = "bazel-compile-commands-extractor-3dddf205a1f5cde20faf2444c1757abe0564ff4c",
        url = "https://github.com/hedronvision/bazel-compile-commands-extractor/archive/3dddf205a1f5cde20faf2444c1757abe0564ff4c.tar.gz",
    )
