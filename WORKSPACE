load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("//3rdparty:workspace.bzl", "maven_dependencies")

maven_dependencies()

load("//3rdparty:target_file.bzl", "build_external_workspace")

build_external_workspace(name = "third_party")

########################################################################################
##################################################
###################
#
#                             scala stuff
#
##############################################################
########################################################################################
SKYLIB_VERSION = "1.0.3"

http_archive(
    name = "bazel_skylib",
    sha256 = "1c531376ac7e5a180e0237938a2536de0c54d93f5c278634818e0efc952dd56c",
    type = "tar.gz",
    url = "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/{}/bazel-skylib-{}.tar.gz".format(SKYLIB_VERSION, SKYLIB_VERSION),
)

RULES_SCALA_VERSION = "887c9be387734d2a49adab441d7a68414e30cbee"

http_archive(
    name = "io_bazel_rules_scala",
    strip_prefix = "rules_scala-%s" % RULES_SCALA_VERSION,
    type = "zip",
    url = "https://github.com/bazelbuild/rules_scala/archive/%s.zip" % RULES_SCALA_VERSION,
)

load("@io_bazel_rules_scala//:scala_config.bzl", "scala_config")

scala_config(scala_version = "3.1.2")

load("@io_bazel_rules_scala//scala:scala.bzl", "scala_repositories")

scala_repositories()

load("@io_bazel_rules_scala//scala:toolchains.bzl", "scala_register_toolchains")

scala_register_toolchains()

load("@io_bazel_rules_scala//scala/scalafmt:scalafmt_repositories.bzl", "scalafmt_default_config", "scalafmt_repositories")

scalafmt_default_config()

scalafmt_repositories()

register_toolchains(
    "//src/main/scala/com/muchq/spitha:diagnostics_reporter_toolchain",
)

########################################################################################
##################################################
###################
#
#                             proto stuff
#
##############################################################
########################################################################################

load("@rules_proto//proto:repositories.bzl", "rules_proto_dependencies", "rules_proto_toolchains")

rules_proto_dependencies()

rules_proto_toolchains()
