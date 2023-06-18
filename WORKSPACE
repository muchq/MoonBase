workspace(name = "moon_base")

load("//bazel:http_archives.bzl", "register_http_archive_dependencies")

register_http_archive_dependencies()

########################################################################################
##################################################
###################
#
#                             go stuff
#
##############################################################
########################################################################################

load("@io_bazel_rules_go//go:deps.bzl", "go_register_toolchains", "go_rules_dependencies")

go_rules_dependencies()

go_register_toolchains(version = "1.20.4")

########################################################################################
##################################################
###################
#
#                             cpp stuff
#
##############################################################
########################################################################################

load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps")

grpc_deps()

load("@com_github_grpc_grpc//bazel:grpc_extra_deps.bzl", "grpc_extra_deps")

grpc_extra_deps()

########################################################################################
##################################################
###################
#
#                             proto stuff
#
##############################################################
########################################################################################

load("@rules_proto_grpc//:repositories.bzl", "rules_proto_grpc_repos", "rules_proto_grpc_toolchains")

rules_proto_grpc_toolchains()

rules_proto_grpc_repos()

load("@rules_proto_grpc//cpp:repositories.bzl", "cpp_repos")

cpp_repos()

load("@rules_proto//proto:repositories.bzl", "rules_proto_dependencies", "rules_proto_toolchains")

rules_proto_dependencies()

rules_proto_toolchains()

########################################################################################
##################################################
###################
#
#                              java stuff
#
##############################################################
########################################################################################

load("//3rdparty:workspace.bzl", "maven_dependencies")

maven_dependencies()

load("//3rdparty:target_file.bzl", "build_external_workspace")

build_external_workspace(name = "third_party")

load("@contrib_rules_jvm//:repositories.bzl", "contrib_rules_jvm_deps")

contrib_rules_jvm_deps()

load("@contrib_rules_jvm//:setup.bzl", "contrib_rules_jvm_setup")

contrib_rules_jvm_setup()

########################################################################################
##################################################
###################
#
#                             scala stuff
#
##############################################################
########################################################################################

bind(
    name = "io_bazel_rules_scala/dependency/scalatest/scalatest",
    actual = "//3rdparty/jvm/org/scalatest",
)

load("@io_bazel_rules_scala//:scala_config.bzl", "scala_config")

scala_config(scala_version = "3.2.1")

load("@io_bazel_rules_scala//scala:scala.bzl", "scala_repositories")

scala_repositories()

load("@io_bazel_rules_scala//scala:toolchains.bzl", "scala_register_toolchains")

scala_register_toolchains()

load("@io_bazel_rules_scala//testing:scalatest.bzl", "scalatest_repositories", "scalatest_toolchain")

scalatest_repositories()

scalatest_toolchain()

load("@io_bazel_rules_scala//scala/scalafmt:scalafmt_repositories.bzl", "scalafmt_default_config", "scalafmt_repositories")

scalafmt_default_config()

scalafmt_repositories()

#############################################################
#############
##               Rust Stuff
###
##############
#############################################################

load("@rules_rust//rust:repositories.bzl", "rules_rust_dependencies", "rust_register_toolchains")

rules_rust_dependencies()

rust_register_toolchains(
    edition = "2021",
    versions = [
        "1.70.0"
    ],
)
